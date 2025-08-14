import os
import cv2
import zarr
import json
import click
import cc3d
import hashlib
import kimimaro
import fastremap
import numpy as np
import networkx as nx
import scipy.interpolate
import scipy.ndimage
from tqdm import tqdm
from typing import Callable
import sklearn.decomposition

    
def downsample(x: np.ndarray, factor: int) -> np.ndarray:
    # Downsample a 3D array by a factor, using max-pooling
    assert x.ndim == 3
    assert x.shape[0] % factor == 0 and x.shape[1] % factor == 0 and x.shape[2] % factor == 0
    if factor == 1:
        return x
    x = x.reshape(x.shape[0] // factor, factor, x.shape[1] // factor, factor, x.shape[2] // factor, factor)
    x = np.max(x, axis=5)
    x = np.max(x, axis=3)
    x = np.max(x, axis=1)
    return x


def extract_inter_terminal_paths(graph: nx.Graph) -> list[list[int]]:
    # Find all simple paths between pairs of terminal nodes
    terminal_nodes = [n for n in graph.nodes() if graph.degree(n) == 1]
    paths = []
    for i, start in enumerate(terminal_nodes):
        for end in terminal_nodes[i+1:]:
            try:
                path = nx.shortest_path(graph, start, end)
                paths.append(path)
            except nx.NetworkXNoPath:
                pass
    return paths


def extract_longest_path(graph: nx.Graph) -> list[int]:
    # Find the one longest path in the graph
    start_node = next(iter(graph.nodes()))
    distances = nx.single_source_shortest_path_length(graph, start_node)
    node1 = max(distances, key=distances.get)
    distances_from_node1 = nx.single_source_shortest_path_length(graph, node1)
    node2 = max(distances_from_node1, key=distances_from_node1.get)
    return nx.shortest_path(graph, node1, node2)


def extract_inter_branch_paths(graph: nx.Graph) -> list[list[int]]:
    # Extract maximal chain-topology subgraphs (inter-branch paths)
    paths = []
    visited_edges = set()
    
    # Find all terminals and branching points
    critical_nodes = [n for n in graph.nodes() if graph.degree(n) != 2]
    
    for node in critical_nodes:
        for neighbor in graph.neighbors(node):
            edge = tuple(sorted([node, neighbor]))
            if edge in visited_edges:
                continue
            
            # Start a chain from this critical node
            path = [node, neighbor]
            visited_edges.add(edge)
            current = neighbor
            
            # Follow the chain while current node has degree 2
            while graph.degree(current) == 2:
                next_nodes = [n for n in graph.neighbors(current) if n != path[-2]]
                if not next_nodes:
                    break
                next_node = next_nodes[0]
                
                edge = tuple(sorted([current, next_node]))
                if edge in visited_edges:
                    break
                
                path.append(next_node)
                visited_edges.add(edge)
                current = next_node
            
            paths.append(path)
    
    return paths


def save_paths(paths: list[list[int]], vertices: np.ndarray, step_size: int, min_length_mm: float,get_cc_label_at_zyx: Callable[[np.ndarray], np.ndarray], prefix: str, output_path: str, voxel_size_um: float):
    num_saved = 0
    for path in paths:
        path_vertices = vertices[path]
        path_length_vx = np.linalg.norm(np.diff(path_vertices, axis=0), axis=-1).sum()
        if path_length_vx * (voxel_size_um / 1000) < min_length_mm:
            continue
        patch_zyxs = convert_path_to_zyxs(path_vertices, step_size, get_cc_label_at_zyx)
        z_min = int(patch_zyxs[..., 0].min())
        z_max = int(patch_zyxs[..., 0].max())
        hash = hashlib.sha256(patch_zyxs.tobytes()).hexdigest()[:8]
        path_id = f'fiber_{prefix}_{z_min}-{z_max}_{hash}'
        saved = save_tifxyz(patch_zyxs, output_path, path_id, step_size, voxel_size_um)
        num_saved += 1 if saved else 0
    return num_saved


def convert_path_to_zyxs(path_vertices_zyx: np.ndarray, step_size: int, get_cc_label_at_zyx: Callable[[np.ndarray], np.ndarray]) -> np.ndarray:

    # Add points to the centerline progressively until it covers the whole fiber. At each step we binary-search
    # over the remaining length of the fiber, to find a point that is step_size from the previous point
    # The search is needed since this distance is in physical space, rather than arc-length along the fiber
    centerline_points = [path_vertices_zyx[0]]
    s = 0  # arc-length coordinate
    cumulative_lengths = np.concatenate([[0], np.linalg.norm(np.diff(path_vertices_zyx, axis=0), axis=-1).cumsum()])
    interpolator = scipy.interpolate.interp1d(cumulative_lengths, path_vertices_zyx, axis=0, kind='linear')
    while s + step_size < cumulative_lengths[-1]:
        s_min = s
        s_max = cumulative_lengths[-1]
        while s_max - s_min > 1.:
            s = (s_min + s_max) / 2
            current_point = interpolator(s)
            current_dist = np.linalg.norm(current_point - centerline_points[-1], axis=-1)
            if current_dist < step_size:
                s_min = s
            else:
                s_max = s
        centerline_points.append(interpolator(s))
    centerline_points = np.stack(centerline_points, axis=0)

    # Convert direction vectors along the fiber, and thus tangential ones across its wider direction    
    along_fiber_directions = np.concatenate([
        [centerline_points[1] - centerline_points[0]], 
        centerline_points[2:] - centerline_points[:-2],
        [centerline_points[-1] - centerline_points[-2]]
    ])
    along_fiber_directions = along_fiber_directions / np.linalg.norm(along_fiber_directions, axis=-1, keepdims=True)
    tangent_directions = get_tangent_direction(centerline_points, along_fiber_directions, get_cc_label_at_zyx)

    # Build the actual quad-surface patch; for now we fix the width to four quads
    surf_zyxs = np.stack([
        centerline_points - 1.5 * tangent_directions * step_size,
        centerline_points - 0.5 * tangent_directions * step_size,
        centerline_points + 0.5 * tangent_directions * step_size,
        centerline_points + 1.5 * tangent_directions * step_size,
    ], axis=0)

    return surf_zyxs


def get_tangent_direction(point_zyx: np.ndarray, direction_zyx: np.ndarray, get_cc_label_at_zyx: Callable[[np.ndarray], np.ndarray]) -> np.ndarray:

    # Get the cc label at input points; should be everywhere equal to the originating skeleton's 
    # segid, however we occasionally miss slightly due to resampling etc.
    label = get_cc_label_at_zyx(point_zyx)
    label = label[label != 0][0]
    
    # Get two orthogonal vectors forming a basis for the plane perpendicular to direction_zyx
    perpendicular_direction_1 = np.cross(direction_zyx, np.asarray([1, 0, 0]))
    perpendicular_direction_1 = np.where(
        np.linalg.norm(perpendicular_direction_1, axis=-1, keepdims=True) < 1.e-4,
        np.cross(direction_zyx, np.asarray([0, 1, 0])),
        perpendicular_direction_1
    )
    perpendicular_direction_1 /= np.linalg.norm(perpendicular_direction_1, axis=-1, keepdims=True)
    perpendicular_direction_2 = np.cross(direction_zyx, perpendicular_direction_1)

    # Form a small grid of coordinates on the perpendicular plane, centered around point_zyx
    # point_zyx and perpendicular_direction_* are indexed [point-along-fiber, zyx]
    # grid_points will be indexed [point-along-fiber, y-in-grid, x-in-grid, zyx]
    grid_size = 32
    grid_spacing = 1
    grid = np.linspace(-grid_size * grid_spacing / 2, grid_size * grid_spacing / 2, grid_size)
    grid_points = point_zyx[:, None, None, :] + grid[None, :, None, None] * perpendicular_direction_1[:, None, None, :] + grid[None, None, :, None] * perpendicular_direction_2[:, None, None, :]

    # Get the labels of the grid points, and thus which ones are on the fiber. We check if there are
    # multiple connected components in the section, and if so take the one at/nearest the center
    grid_points_on_fiber = get_cc_label_at_zyx(grid_points) == label
    for point_idx in range(len(grid_points)):
        ccs, num_ccs = cc3d.connected_components(grid_points_on_fiber[point_idx], connectivity=4, return_N=True, binary_image=True)
        if num_ccs <= 1:
            continue
        main_cc_label = ccs[grid.shape[0] // 2, grid.shape[0] // 2]
        if main_cc_label == 0:
            centroids = cc3d.statistics(ccs)['centroids'][1:]
            centroid_distances = np.linalg.norm(centroids - grid.shape[0] // 2, axis=-1)
            main_cc_label = np.argmin(centroid_distances) + 1
        grid_points_on_fiber[point_idx] = ccs == main_cc_label

    # For each point on the fiber, find the first principal component of the fiber points on the perpendicular plane
    first_pcs = []
    for point_idx in range(len(grid_points)):
        coordinates_on_fiber = grid_points[point_idx][grid_points_on_fiber[point_idx]]
        if len(coordinates_on_fiber) < 2:
            first_pcs.append([1, 0, 0])
            continue
        pca = sklearn.decomposition.PCA(n_components=1)
        pca.fit(coordinates_on_fiber)
        first_pcs.append(pca.components_[0])

    # Flip all to have positive z-component
    # TODO: properly handle the case where the pc has zero z-component
    first_pcs = np.stack(first_pcs, axis=0)
    z_signs = np.sign(first_pcs[..., 0])
    first_pcs = first_pcs * np.where(z_signs == 0, 1, z_signs)[..., None]

    if False:  # debug: plot the perpendicular planes, colored according to whether points are on the fiber
        import matplotlib.pyplot as plt
        fig = plt.figure()
        ax = fig.add_subplot(projection='3d')
        ax.scatter(grid_points.reshape(-1, 3)[:, 0], grid_points.reshape(-1, 3)[:, 1], grid_points.reshape(-1, 3)[:, 2], c=grid_points_on_fiber.reshape(-1))
        for point_idx in range(len(grid_points)):
            start = point_zyx[point_idx]
            end = start + first_pcs[point_idx] * 50
            ax.plot([start[0], end[0]], [start[1], end[1]], [start[2], end[2]], color='red')
        plt.show()

    return first_pcs


def save_tifxyz(zyxs, path, uuid, step_size, voxel_size_um):
    path = f'{path}/{uuid}'
    if os.path.exists(path):
        # We skip existing since two paths that are technically different may end up with the same vertices after discretisation to tifxyz
        return False
    os.makedirs(path, exist_ok=True)
    cv2.imwrite(f'{path}/x.tif', zyxs[..., 2])
    cv2.imwrite(f'{path}/y.tif', zyxs[..., 1])
    cv2.imwrite(f'{path}/z.tif', zyxs[..., 0])
    area_vx2 = (zyxs.shape[0] - 1) * (zyxs.shape[1] - 1) * step_size ** 2
    with open(f'{path}/meta.json', 'w') as f:
        json.dump({
            'scale': [1 / step_size, 1 / step_size],
            'bbox': [zyxs.min(axis=(0, 1))[::-1].tolist(), zyxs.max(axis=(0, 1))[::-1].tolist()],
            'area_vx2': area_vx2,
            'area_cm2': area_vx2 * voxel_size_um ** 2 / 1.e8,
            'format': 'tifxyz',
            'type': 'seg',
            'uuid': uuid,
            'source': 'horizontal_fibers_to_patches',
        }, f, indent=4)
    return True


@click.command()
@click.option('--predictions-zarr-path', required=True, help='Path/URL to predictions OME-Zarr')
@click.option('--predictions-ome-scale', type=int, required=True, help='OME scaling level to read from predictions')
@click.option('--voxel-size', type=float, required=True, help='Original voxel size in microns')
@click.option('--output-path', required=True, help='Folder to write tifxyz patches to')
@click.option('--z-min', type=int, default=0, help='First slice (wrt original scan)')
@click.option('--z-max', type=int, default=None, help='Last slice (wrt original scan)')
@click.option('--step-size', type=int, default=20, help='Step size for surface (default: 20)')
@click.option('--cc-chunk-depth', type=int, default=160, help='Chunk depth for connected components (wrt predictions) (default: 160)')
@click.option('--skeletonisation-chunk-depth', type=int, default=160, help='Chunk depth for skeletonisation (wrt predictions) (default: 160)')
@click.option('--skeletonisation-chunk-overlap', type=int, default=0, help='Overlap of adjacent skeletonisation chunks (default: 0)')
@click.option('--skeletonisation-downsample-factor', type=int, default=2, help='Downsample factor prior to skeletonisation (default: 2)')
@click.option('--dust-threshold', type=int, default=8000, help='Minimum voxel count to keep a component (default: 8000)')
@click.option('--min-length', type=int, default=10, help='Minimum length of a path in mm (default: 10)')
def main(
    predictions_zarr_path,
    predictions_ome_scale,
    voxel_size,
    output_path,
    z_min,
    z_max,
    step_size,
    cc_chunk_depth,
    skeletonisation_chunk_depth,
    skeletonisation_chunk_overlap,
    skeletonisation_downsample_factor,
    dust_threshold,
    min_length,
):
    print(f'loading {predictions_zarr_path}/{predictions_ome_scale}...')
    predictions_zarr_array = zarr.open(f'{predictions_zarr_path}/{predictions_ome_scale}', mode='r')

    z_min //= 2 ** predictions_ome_scale
    if z_max is None:
        z_max = predictions_zarr_array.shape[0]
    else:
        z_max //= 2 ** predictions_ome_scale

    # TODO: should z_min/_max be wrt origin volume, regardless of chosen zarr scale?

    assert z_min >= 0 and z_min < predictions_zarr_array.shape[0], f'z_min must be in [0, {predictions_zarr_array.shape[0] * 2 ** predictions_ome_scale})'
    assert z_max > z_min and z_max <= predictions_zarr_array.shape[0], f'z_max must be in ({z_min}, {predictions_zarr_array.shape[0] * 2 ** predictions_ome_scale})'
    assert skeletonisation_chunk_overlap < skeletonisation_chunk_depth, 'skeletonisation chunk overlap must be less than the chunk depth'

    def get_chunks():
        for z_chunk_min in range(z_min, z_max, cc_chunk_depth):

            z_chunk_max = min(z_chunk_min + cc_chunk_depth, z_max, predictions_zarr_array.shape[0] - 2)  # -2 is to work around a crackle bug
            print(f'  loading slices {z_chunk_min}-{z_chunk_max}')
            predictions = predictions_zarr_array[z_chunk_min : z_chunk_max]

            # We assume 0 = background, 1 = vertical, 2 = horizontal, 3 = ambiguous
            print('  filtering to horizontal fibers')
            predictions = fastremap.remap(predictions, {0: 0, 1: 0, 2: 1, 3: 1})

            print('  yielding to connected_components_stack')
            yield predictions.transpose(1, 2, 0)  # zyx --> yxz

    print('extracting connected components')
    cc_labels_crackle, num_ccs = cc3d.connected_components_stack(get_chunks(), connectivity=6, return_N=True, binary_image=True)
    print(f'found {num_ccs} connected components in total')
    # Note cc_labels_crackle is indexed in yxz order!

    # TODO: could merge skeletons across z-chunks (see kimimaro simple_merge example); however horizontal 
    # fibers probably don't cross chunk boundaries too often

    total_paths = 0
    for z_chunk_min in range(z_min, z_max, skeletonisation_chunk_depth - skeletonisation_chunk_overlap):
        print(f'skeletonising chunk starting at slice {z_chunk_min}')

        z_chunk_max = min(z_chunk_min + skeletonisation_chunk_depth, z_max, predictions_zarr_array.shape[0] - 2)  # -2 is to work around a crackle bug
        cc_labels = cc_labels_crackle[:, :, z_chunk_min - z_min : z_chunk_max - z_min].transpose(2, 0, 1)
        cc_labels_ds = downsample(cc_labels, skeletonisation_downsample_factor)

        print('  skeletonising')
        overall_downsample_factor = skeletonisation_downsample_factor * 2 ** predictions_ome_scale  # scale factor of cc_labels vs original volume
        skeletons = kimimaro.skeletonize(
            cc_labels_ds,
            teasar_params={
                "scale": 2.,  # >1 increases the invalidation ball size beyond the width of the current structure
                "const": 16. / overall_downsample_factor,  # should be somewhat larger than fibre width (after downsampling; 8 is a generous estimate of fibre width at /1 scale); we will ignore branches smaller than this
            },
            anisotropy=(overall_downsample_factor, overall_downsample_factor, overall_downsample_factor),  # ...so the skeletons are in original volume (zarr /0) space
            dust_threshold=dust_threshold // overall_downsample_factor**3,
            fix_branching=False,
            fix_borders=False,
            fill_holes=False,
            progress=True,
            # TODO: make it a command line arg
            parallel=8,  # <= 0 all cpu, 1 single process, 2+ multiprocess
            parallel_chunk_size=50,  # how many skeletons to submit as a block to each worker
            in_place=True,
        )

        def get_cc_label_at_zyx(zyx: np.ndarray) -> np.ndarray:  # zyx is given wrt the original volume
            zyx_in_chunk = (zyx - [z_chunk_min * 2 ** predictions_ome_scale, 0, 0]) // 2 ** predictions_ome_scale
            return scipy.ndimage.map_coordinates(cc_labels, np.moveaxis(zyx_in_chunk, -1, 0), order=0, mode='constant', cval=0)

        progress_bar = tqdm(skeletons.values(), desc='  saving paths')
        for skeleton in progress_bar:
            graph = nx.Graph()
            graph.add_edges_from(skeleton.edges)

            skeleton_vertices_zyx = np.asarray(skeleton.vertices, dtype=np.int32) + [z_chunk_min * 2 ** predictions_ome_scale, 0, 0]

            # Longest possible path through the skeleton
            longest_path = extract_longest_path(graph)
            num_saved = save_paths([longest_path], skeleton_vertices_zyx, step_size, min_length, get_cc_label_at_zyx, 'lo', output_path, voxel_size)
            total_paths += num_saved

            # # 'Optimistic' paths that span the entire skeleton
            # # TODO: disabled for now since this results in lots of near-identical paths
            # inter_terminal_paths = extract_inter_terminal_paths(graph)
            # num_saved = save_paths(inter_terminal_paths, skeleton_vertices_zyx, step_size, min_length, get_cc_label_at_zyx, 'it', output_path, voxel_size)
            # total_paths += num_saved
            inter_terminal_paths = []

            # 'Conservative' paths that stop at branching points
            inter_branch_paths = extract_inter_branch_paths(graph)
            inter_terminal_paths_set = set(map(tuple, inter_terminal_paths + [longest_path]))
            inter_branch_paths = [path for path in inter_branch_paths if tuple(path) not in inter_terminal_paths_set]
            num_saved = save_paths(inter_branch_paths, skeleton_vertices_zyx, step_size, min_length, get_cc_label_at_zyx, 'ib', output_path, voxel_size)
            total_paths += num_saved

            progress_bar.set_postfix(total_paths=total_paths)

    print(f'found {total_paths} paths in total')


if __name__ == '__main__':
    main()
