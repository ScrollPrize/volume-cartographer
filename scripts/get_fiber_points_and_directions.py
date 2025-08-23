import torch
import torch.nn.functional as F
import zarr
import click
import cc3d
import fastremap
import numpy as np
import scipy.interpolate
import scipy.ndimage
from tqdm import tqdm
from collections import deque


def downsample(array, factor):
    """Simple downsampling by taking every nth element along each axis."""
    return array[::factor, ::factor, ::factor]


class StructureTensor:

    def __init__(self, prefilter_sigma=1.5, windowing_sigma=2.):

        # Inspired by vesuvius.structure_tensor.create_st

        def make_gauss3d(sigma):
            radius = int(3 * sigma)
            coords = torch.arange(-radius, radius + 1, dtype=torch.float32, device='cuda')
            g1 = torch.exp(-coords**2 / (2 * prefilter_sigma**2))
            g1 = g1 / g1.sum()
            return g1[:, None, None] * g1[None, :, None] * g1[None, None, :], radius
        self._prefilter_gauss3d, self._prefilter_pad = make_gauss3d(prefilter_sigma)
        self._windowing_gauss3d, self._windowing_pad = make_gauss3d(windowing_sigma)

        # See http://www.holoborodko.com/pavel/image-processing/edge-detection/
        d = torch.tensor([2.,1.,-16.,-27.,0.,27.,16.,-1.,-2.], device='cuda') # derivative kernel
        s = torch.tensor([1., 4., 6., 4., 1.], device='cuda') # smoothing kernel
        self._pavel_kz = (d.view(9,1,1) * s.view(1,5,1) * s.view(1,1,5)) / (96*16*16)  # depth derivative with y/x smoothing
        self._pavel_ky = (s.view(5,1,1) * d.view(1,9,1) * s.view(1,1,5)) / (96*16*16)  # height derivative with z/x smoothing
        self._pavel_kx = (s.view(5,1,1) * s.view(1,5,1) * d.view(1,1,9)) / (96*16*16)  # width derivative with z/y smoothing


    def compute_structure_tensor(self, x: torch.Tensor):

        x = torch.from_numpy(x).to(torch.float32)[None,None].cuda()
        
        x = F.conv3d(x, self._prefilter_gauss3d[None, None], padding=(self._prefilter_pad,) * 3)

        gz = F.conv3d(x, self._pavel_kz[None, None], padding=(4, 2, 2))
        gy = F.conv3d(x, self._pavel_ky[None, None], padding=(2, 4, 2))
        gx = F.conv3d(x, self._pavel_kx[None, None], padding=(2, 2, 4))

        J = torch.stack([
            gz * gz, gy * gz, gx * gz,
            gy * gy, gx * gy,
            gx * gx,
        ], dim=0).squeeze(1)  # triu-element, 1, z, y, x

        S = F.conv3d(J, self._windowing_gauss3d[None, None], padding=(self._windowing_pad,) * 3).squeeze(1)

        # S is now indexed by element, depth, height, width; element indexes over upper triangle of a 3x3 symmetric metrics

        matrices = torch.zeros([3, 3, *S.shape[1:]], device=S.device, dtype=S.dtype)
        indices = torch.triu_indices(3, 3, device=S.device)
        for idx, ji in enumerate(indices.T):
            matrices[*ji] = S[idx]

        if np.prod(matrices.shape[2:]) > 2**20:
            eigenvecs = torch.stack([
                torch.linalg.eigh(matrices.permute(2, 3, 4, 0, 1)[i], UPLO='U').eigenvectors
                for i in range(matrices.shape[2])
            ], dim=0)
        else:
            eigenvecs = torch.linalg.eigh(matrices.permute(2, 3, 4, 0, 1), UPLO='U').eigenvectors  # returned in ascending order of eigenvalues

        min_grad_eigenvec = eigenvecs[..., :, 0]
        return min_grad_eigenvec.cpu().numpy()


def comb_fiber(directions, mask):
    """
    Consistently orient 3D vectors using flood-fill propagation.
    
    Args:
        directions: (Z, Y, X, 3) array of 3D vectors
        mask: (Z, Y, X) boolean mask indicating valid positions
    
    Returns:
        consistently oriented directions array
    """
    # Copy directions to avoid modifying input
    oriented_directions = directions.copy()
    
    # Get valid indices
    valid_coords = np.stack(np.where(mask), axis=-1)
    if len(valid_coords) == 0:
        return oriented_directions
    
    # Track visited points globally
    visited = np.zeros_like(mask, dtype=bool)
    
    # 6-connectivity neighbors (face-adjacent) as numpy array for efficiency
    neighbors_offsets = np.array([[-1,0,0], [1,0,0], [0,-1,0], [0,1,0], [0,0,-1], [0,0,1]])
    
    # Process all connected components
    for start_idx in range(len(valid_coords)):
        start_coord = tuple(valid_coords[start_idx])
        
        # Skip if already processed
        if visited[start_coord]:
            continue
            
        # BFS for this connected component
        queue = deque([start_coord])
        visited[start_coord] = True
        
        while queue:
            current = queue.popleft()
            current_vec = oriented_directions[current]
            
            # Compute all neighbor coordinates at once
            neighbors = np.array(current) + neighbors_offsets
            
            # Check bounds for all neighbors
            valid_bounds = ((neighbors >= 0) & (neighbors < np.array(mask.shape))).all(axis=1)
            
            # Collect valid neighboring vectors for consensus
            neighbor_vecs = []
            neighbor_coords = []
            
            for i, neighbor_coord in enumerate(neighbors):
                if not valid_bounds[i]:
                    continue
                    
                neighbor = tuple(neighbor_coord)
                
                if mask[neighbor]:
                    if visited[neighbor]:
                        # Use already oriented neighbors for consensus
                        neighbor_vecs.append(oriented_directions[neighbor])
                        neighbor_coords.append(neighbor)
                    elif not visited[neighbor]:
                        # This is a new neighbor to process
                        neighbor_coords.append(neighbor)
                        neighbor_vecs.append(oriented_directions[neighbor])
            
            # Process unvisited neighbors
            for j, neighbor in enumerate(neighbor_coords):
                if not visited[neighbor]:
                    neighbor_vec = oriented_directions[neighbor]
                    
                    # Use consensus of already processed neighbors if available
                    if len([v for k, v in enumerate(neighbor_vecs) if visited[neighbor_coords[k]]]) > 0:
                        consensus_vecs = [oriented_directions[neighbor_coords[k]] 
                                        for k in range(len(neighbor_coords)) 
                                        if visited[neighbor_coords[k]]]
                        
                        # Vote based on multiple neighbors
                        votes_normal = sum(np.dot(ref_vec, neighbor_vec) for ref_vec in consensus_vecs)
                        votes_flipped = sum(np.dot(ref_vec, -neighbor_vec) for ref_vec in consensus_vecs)
                        
                        if votes_flipped > votes_normal:
                            oriented_directions[neighbor] = -neighbor_vec
                    else:
                        # Fall back to single neighbor comparison
                        dot_normal = np.dot(current_vec, neighbor_vec)
                        dot_flipped = np.dot(current_vec, -neighbor_vec)
                        
                        if abs(dot_flipped) > abs(dot_normal):
                            oriented_directions[neighbor] = -neighbor_vec
                    
                    # Add to queue and mark as visited
                    queue.append(neighbor)
                    visited[neighbor] = True
    
    return oriented_directions


def comb_global(fiber_points, fiber_directions, subsample_factor=10):
    """
    Globally orient fibers to be consistent with each other.
    Uses nearest-neighbor propagation to ensure all fibers are processed.
    
    Args:
        fiber_points: List of (N_i, 3) arrays, each containing 3D points of a fiber
        fiber_directions: List of (N_i, 3) arrays, each containing direction vectors
        subsample_factor: Use every nth point for efficiency in proximity calculations
        
    Returns:
        Tuple of (oriented_points, oriented_directions) with globally consistent orientations
    """
    
    n_fibers = len(fiber_points)
    
    oriented_directions = [None] * n_fibers
    visited = np.zeros(n_fibers, dtype=bool)
    
    # Precompute all pairwise fiber distances for efficiency
    fiber_distances = np.zeros((n_fibers, n_fibers))
    for i in tqdm(range(n_fibers), desc="computing distances", leave=False):
        points_i = fiber_points[i][::subsample_factor]
        for j in range(i + 1, n_fibers):
            points_j = fiber_points[j][::subsample_factor]
            diff = points_i[:, None, :] - points_j[None, :, :]
            distances = np.linalg.norm(diff, axis=2)
            dist = np.percentile(distances, 25)
            fiber_distances[i, j] = fiber_distances[j, i] = dist
    
    def find_nearest_unvisited_fiber(visited_set):
        """Find the nearest unvisited fiber to any fiber in the visited set."""
        min_distance = float('inf')
        nearest_fiber = None
        
        for unvisited_idx in range(n_fibers):
            if visited[unvisited_idx]:
                continue
                
            # Find minimum distance to any visited fiber
            min_dist_to_visited = float('inf')
            for visited_idx in visited_set:
                dist = fiber_distances[unvisited_idx, visited_idx]
                min_dist_to_visited = min(min_dist_to_visited, dist)
            
            if min_dist_to_visited < min_distance:
                min_distance = min_dist_to_visited
                nearest_fiber = unvisited_idx
        
        return nearest_fiber
    
    def get_visited_neighbors(fiber_idx, visited_set, max_neighbors=5):
        """Get the closest visited neighbors for consistency checking."""
        neighbors = []
        distances = []
        
        for visited_idx in visited_set:
            if visited_idx != fiber_idx:
                dist = fiber_distances[fiber_idx, visited_idx]
                neighbors.append(visited_idx)
                distances.append(dist)
        
        sorted_pairs = sorted(zip(distances, neighbors))
        return [neighbor for _, neighbor in sorted_pairs[:max_neighbors]]
    
    def is_consistent_orientation(fiber_idx, neighbor_indices):
        """Check if fiber direction is consistent with neighbors."""
            
        # Sample a subset of points from the current fiber for the consistency check
        sample_indices = np.arange(0, len(fiber_directions[fiber_idx]), max(1, len(fiber_directions[fiber_idx]) // 20))
        points_curr = fiber_points[fiber_idx][sample_indices]
        dirs_curr = fiber_directions[fiber_idx][sample_indices]
        
        consistency_votes = 0
        total_votes = 0
        for neighbor_idx in neighbor_indices:
            points_neighbor = fiber_points[neighbor_idx]
            dirs_neighbor = oriented_directions[neighbor_idx]
            for point_curr, dir_curr in zip(points_curr, dirs_curr):
                # Find closest point in neighbor fiber; check if its direction is consistent
                distances = np.linalg.norm(points_neighbor - point_curr, axis=1)
                closest_idx_in_neighbor = np.argmin(distances)
                dir_neighbor = dirs_neighbor[closest_idx_in_neighbor]
                dot_product = np.dot(dir_curr, dir_neighbor)
                if dot_product > 0:
                    consistency_votes += 1
                total_votes += 1
        
        return total_votes == 0 or consistency_votes > total_votes / 2
    
    # Start with longest fiber as seed; keep its original orientation
    seed_fiber = np.argmax([len(points) for points in fiber_points])
    visited[seed_fiber] = True
    visited_set = {seed_fiber}
    oriented_directions[seed_fiber] = fiber_directions[seed_fiber]
    
     # Repeatedly choose the nearest unvisited fiber to the visited set, and orient it
     # to be consistent with its neighbours in the visited set
    with tqdm(total=n_fibers-1, desc="combing fibers") as pbar:
        while len(visited_set) < n_fibers:
            next_fiber = find_nearest_unvisited_fiber(visited_set)
            reference_fibers = get_visited_neighbors(next_fiber, visited_set)
            if is_consistent_orientation(next_fiber, reference_fibers):
                oriented_directions[next_fiber] = fiber_directions[next_fiber]
            else:
                oriented_directions[next_fiber] = -fiber_directions[next_fiber]
            visited[next_fiber] = True
            visited_set.add(next_fiber)
            pbar.update(1)

    if False:
        import matplotlib.pyplot as plt
        ax = plt.figure().add_subplot(projection='3d')
        ax.quiver(*fiber_points[seed_fiber].T, *oriented_directions[seed_fiber].T)
        ax.set_aspect('equal')
        plt.show()

    return oriented_directions 


def get_directions(cc_mask, structure_tensor: StructureTensor):
    fiber_directions = structure_tensor.compute_structure_tensor(cc_mask)
    fiber_directions = comb_fiber(fiber_directions, cc_mask)
    if False:  # debug: plot as arrows
        import matplotlib.pyplot as plt
        zyx = np.stack(np.meshgrid(np.arange(fiber_directions.shape[0]), np.arange(fiber_directions.shape[1]), np.arange(fiber_directions.shape[2]), indexing='ij'), axis=-1)
        cc_mask_bool = cc_mask > 0
        ax = plt.figure().add_subplot(projection='3d')
        ax.quiver(*zyx[cc_mask_bool].T, *fiber_directions[cc_mask_bool].T)
        ax.set_aspect('equal')
        plt.show()
    return fiber_directions


@click.command()
@click.option('--predictions-zarr-path', required=True, help='Path/URL to predictions OME-Zarr')
@click.option('--predictions-ome-scale', type=int, required=True, help='OME scaling level to read from predictions')
@click.option('--sparse-output-path', required=True, help='File to write fiber points and directions to')
@click.option('--dense-output-path', required=True, help='File to write dense direction field to')
@click.option('--dense-output-ome-scale', type=int, required=True, help='OME scaling level for dense direction field (equal or greater than predictions-ome-scale)')
@click.option('--z-min', type=int, default=0, help='First slice (wrt original scan)')
@click.option('--z-max', type=int, default=None, help='Last slice (wrt original scan)')
@click.option('--dust-threshold', type=int, default=2000, help='Minimum voxel count to keep a component (default: 2000)')
@click.option('--chunk-size-zyx', type=int, nargs=3, default=[256, 256, 64], help='zyx chunk size for processing (wrt predictions ome scale)')
def main(
    predictions_zarr_path,
    predictions_ome_scale,
    sparse_output_path,
    dense_output_path,
    dense_output_ome_scale,
    z_min,
    z_max,
    dust_threshold,
    chunk_size_zyx,
):
    print(f'loading {predictions_zarr_path}/{predictions_ome_scale}...')
    predictions_zarr_array = zarr.open(f'{predictions_zarr_path}/{predictions_ome_scale}', mode='r')

    z_min //= 2 ** predictions_ome_scale
    if z_max is None:
        z_max = predictions_zarr_array.shape[0]
    else:
        z_max //= 2 ** predictions_ome_scale

    assert z_min >= 0 and z_min < predictions_zarr_array.shape[0], f'z_min must be in [0, {predictions_zarr_array.shape[0] * 2 ** predictions_ome_scale})'
    assert z_max > z_min and z_max <= predictions_zarr_array.shape[0], f'z_max must be in ({z_min}, {predictions_zarr_array.shape[0] * 2 ** predictions_ome_scale})'
    assert dense_output_ome_scale >= predictions_ome_scale, f'output-dense-ome-scale must be greater than or equal to predictions-ome-scale'

    dense_output_downsample = 2 ** (dense_output_ome_scale - predictions_ome_scale)

    chunk_z, chunk_y, chunk_x = chunk_size_zyx

    structure_tensor = StructureTensor()

    total_points = 0
    sparse_out_fp = open(sparse_output_path, 'wb')
    dense_out_zarr = zarr.open(dense_output_path, mode='w')
    dense_out_ds = dense_out_zarr.create_dataset(
        'directions',
        dtype=np.float16,
        shape=(predictions_zarr_array.shape[0] // dense_output_downsample, predictions_zarr_array.shape[1] // dense_output_downsample, predictions_zarr_array.shape[2] // dense_output_downsample, 3),
        chunks=(128, 128, 128, 3),
        compressor=zarr.Blosc(cname='zstd', clevel=1, shuffle=True),
        write_empty_chunks=False,
    )
    
    for z_start in range(z_min, z_max, chunk_z):
        for y_start in range(0, predictions_zarr_array.shape[1], chunk_y):
            for x_start in range(0, predictions_zarr_array.shape[2], chunk_x):
                z_end = min(z_start + chunk_z, z_max, predictions_zarr_array.shape[0])
                y_end = min(y_start + chunk_y, predictions_zarr_array.shape[1])
                x_end = min(x_start + chunk_x, predictions_zarr_array.shape[2])
                
                print(f'processing chunk [{z_start}:{z_end}, {y_start}:{y_end}, {x_start}:{x_end}]')

                chunk = predictions_zarr_array[z_start:z_end, y_start:y_end, x_start:x_end]
                
                # We assume 0 = background, 1 = vertical, 2 = horizontal, 3 = ambiguous
                print('  filtering to horizontal fibers')
                chunk = fastremap.remap(chunk, {0: 0, 1: 0, 2: 1, 3: 1})

                print('  running connected components')
                chunk_ccs, num_ccs = cc3d.connected_components(chunk, connectivity=6, return_N=True)
                print(f'  found {num_ccs} connected components')
                chunk_ccs, num_ccs = cc3d.dust(chunk_ccs, dust_threshold // 2**predictions_ome_scale, return_N=True, precomputed_ccl=True)
                chunk_ccs, _ = fastremap.renumber(chunk_ccs, preserve_zero=True)
                print(f'  retained {num_ccs} after dusting')

                if num_ccs == 0:
                    continue

                chunk_points_zyx = []
                chunk_directions_zyx = []

                cc_stats = cc3d.statistics(chunk_ccs, no_slice_conversion=True)
                for cc_idx, cc_mask in tqdm(cc3d.each(chunk_ccs, binary=False, in_place=True), total=num_ccs):
                    cc_z_min, cc_z_max, cc_y_min, cc_y_max, cc_x_min, cc_x_max = cc_stats['bounding_boxes'][cc_idx]
                    cc_mask_cropped = cc_mask[cc_z_min:cc_z_max, cc_y_min:cc_y_max, cc_x_min:cc_x_max]
                    directions_zyx = get_directions(cc_mask_cropped, structure_tensor)
                    points_zyx = np.stack(np.where(cc_mask_cropped), axis=-1)
                    directions_zyx = directions_zyx[*points_zyx.T]
                    points_zyx = points_zyx + np.array([cc_z_min, cc_y_min, cc_x_min])
                    points_zyx_orig = (points_zyx + np.array([z_start, y_start, x_start])) * 2 ** predictions_ome_scale
                    # TODO: consider trimming off the last/first fraction of the fiber since the directions there tend to be less reliable
                    #  That is non-trivial since it requires figuring where in the point-cloud the ends are!
                    points_and_directions = np.stack([points_zyx_orig, directions_zyx], axis=1).astype(np.float32)
                    # TODO: consider using uint16 for indices, and float16 for directions
                    #  In that case have to strictly alternate between points and directions
                    # TODO: write the globally-combed version to sparse file also?
                    sparse_out_fp.write(points_and_directions.tobytes())
                    chunk_points_zyx.append(points_zyx)
                    chunk_directions_zyx.append(directions_zyx)
                    total_points += len(points_zyx)
                    if False:
                        import matplotlib.pyplot as plt
                        ax = plt.figure().add_subplot(projection='3d')
                        ax.quiver(*points_zyx.T, *directions_zyx.T)
                        ax.set_aspect('equal')
                        plt.show()

                print('  combing...')
                # FIXME: should comb across chunks too! maintain a cache of recent-z-chunk fibers
                chunk_directions_zyx = comb_global(chunk_points_zyx, chunk_directions_zyx)

                print('  interpolating...')
                xi = np.stack(np.meshgrid(
                    np.arange(0, z_end - z_start, dense_output_downsample),
                    np.arange(0, y_end - y_start, dense_output_downsample),
                    np.arange(0, x_end - x_start, dense_output_downsample),
                    indexing='ij'
                ), axis=-1)
                # TODO: could make this more efficient by ignoring interior points
                # TODO: could retain original high-resolution points where valid instead of re-interpolating (though, sparse)
                dense_directions_linear = scipy.interpolate.griddata(
                    points=np.concatenate(chunk_points_zyx, axis=0),
                    values=np.concatenate(chunk_directions_zyx, axis=0),
                    xi=xi.reshape(-1, 3),
                    method='linear'
                ).reshape(xi.shape)
                dense_directions_nearest = scipy.interpolate.griddata(
                    points=np.concatenate(chunk_points_zyx, axis=0),
                    values=np.concatenate(chunk_directions_zyx, axis=0),
                    xi=xi.reshape(-1, 3),
                    method='nearest'
                ).reshape(xi.shape)
                dense_directions = np.where(np.isnan(dense_directions_linear), dense_directions_nearest, dense_directions_linear)
                dense_directions /= np.linalg.norm(dense_directions, axis=-1, keepdims=True)
                dense_out_ds[z_start // dense_output_downsample : z_end // dense_output_downsample, y_start // dense_output_downsample : y_end // dense_output_downsample, x_start // dense_output_downsample : x_end // dense_output_downsample] = dense_directions.astype(np.float16)

    sparse_out_fp.close()
    print(f'extracted {total_points} fiber-points in total')


if __name__ == '__main__':
    main()
