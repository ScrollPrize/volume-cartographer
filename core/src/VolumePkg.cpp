#include "vc/core/types/VolumePkg.hpp"

#include <functional>
#include <set>
#include <utility>

#include "vc/core/util/DateTime.hpp"
#include "vc/core/util/Logging.hpp"
#include "vc/core/util/String.hpp"

using namespace volcart;

namespace fs = volcart::filesystem;

namespace
{
////// Convenience vars and fns for accessing VolumePkg sub-paths //////
constexpr auto CONFIG = "config.json";

inline auto VolsDir(const fs::path& baseDir) -> fs::path
{
    return baseDir / "volumes";
}

inline auto SegsDir(const fs::path& baseDir, const std::string& dirName = "paths") -> fs::path
{
    return baseDir / dirName;
}

inline auto RendDir(const fs::path& baseDir) -> fs::path
{
    return baseDir / "renders";
}

inline auto TfmDir(const fs::path& baseDir) -> fs::path
{
    return baseDir / "transforms";
}

inline auto PreviewDirs(const fs::path& baseDir) -> std::vector<filesystem::path>
{
    return { baseDir / "volumes_preview_half", baseDir / "volumes_masked", baseDir / "volumes_previews"};
}

inline auto ReqDirs(const fs::path& baseDir) -> std::vector<filesystem::path>
{
    return {
        baseDir, ::VolsDir(baseDir), ::SegsDir(baseDir), ::RendDir(baseDir),
        ::TfmDir(baseDir)};
}

inline void keep(const fs::path& dir)
{
    if (not fs::exists(dir / ".vckeep")) {
        std::ofstream(dir / ".vckeep", std::ostream::ate);
    }
}

////// Upgrade functions //////
auto VolpkgV3ToV4(const Metadata& meta) -> Metadata
{
    // Nothing to do
    if (meta.get<int>("version") != 3) {
        return meta;
    }
    Logger()->info("Performing v4 migrations");

    // VolumePkg path
    const auto path = meta.path().parent_path();

    // Write the new volpkg metadata
    Logger()->debug("- Creating primary metadata");
    Metadata newMeta;
    newMeta.set("version", 4);
    newMeta.set("name", meta.get<std::string>("volumepkg name"));
    newMeta.set("materialthickness", meta.get<double>("materialthickness"));
    newMeta.save(path / "config.json");

    // Make the "volumes" directory
    Logger()->debug("- Creating volumes directory");
    const fs::path volumesDir = path / "volumes";
    if (!fs::exists(volumesDir)) {
        fs::create_directory(volumesDir);
    }

    // Set up a new Volume name and make a new folder for it
    // Move the slices
    Logger()->debug("- Migrating v3 volume");
    const auto id = DateTime();
    const auto newVolDir = volumesDir / id;
    fs::rename(path / "slices", newVolDir);

    // Setup and save the metadata to the new Volume folder
    Metadata volMeta;
    volMeta.set("uuid", id);
    volMeta.set("name", id);
    volMeta.set("width", meta.get<int>("width"));
    volMeta.set("height", meta.get<int>("height"));
    volMeta.set("slices", meta.get<int>("number of slices"));
    volMeta.set("voxelsize", meta.get<double>("voxelsize"));
    volMeta.set("min", meta.get<double>("min"));
    volMeta.set("max", meta.get<double>("max"));
    volMeta.save(newVolDir / "meta.json");

    return newMeta;
}

auto VolpkgV4ToV5(const Metadata& meta) -> Metadata
{
    // Nothing to do check
    if (meta.get<int>("version") != 4) {
        return meta;
    }
    Logger()->info("Performing v5 migrations");

    // VolumePkg path
    const auto path = meta.path().parent_path();

    // Add metadata to all the segmentations
    Logger()->debug("- Initializing segmentation metadata");
    fs::path seg;
    const fs::path segsDir = path / "paths";
    for (const auto& entry : fs::directory_iterator(segsDir)) {
        if (fs::is_directory(entry)) {
            // Get the folder as a fs::path
            seg = entry;

            // Generate basic metadata
            Metadata segMeta;
            segMeta.set("uuid", seg.stem().string());
            segMeta.set("name", seg.stem().string());
            segMeta.set("type", "seg");

            // Link the metadata to the vcps file
            if (fs::exists(seg / "pointset.vcps")) {
                segMeta.set("vcps", "pointset.vcps");
            } else {
                segMeta.set("vcps", std::string{});
            }

            // Save the new metadata
            segMeta.save(seg / "meta.json");
        }
    }

    // Add renders folder
    Logger()->debug("- Adding renders directory");
    const fs::path rendersDir = path / "renders";
    if (!fs::exists(rendersDir)) {
        fs::create_directory(rendersDir);
    }

    // Update the version
    auto newMeta = meta;
    newMeta.set("version", 5);
    newMeta.save();

    return newMeta;
}

auto VolpkgV5ToV6(const Metadata& meta) -> Metadata
{
    // Nothing to do check
    if (meta.get<int>("version") != 5) {
        return meta;
    }
    Logger()->info("Performing v6 migrations");

    // VolumePkg path
    const auto path = meta.path().parent_path();

    // Add metadata to all the volumes
    Logger()->debug("- Updating volume metadata");
    fs::path vol;
    const fs::path volsDir = path / "volumes";
    for (const auto& entry : fs::directory_iterator(volsDir)) {
        if (fs::is_directory(entry)) {
            // Get the folder as a fs::path
            vol = entry;

            // Generate basic metadata
            Metadata volMeta(vol / "meta.json");
            if (!volMeta.hasKey("uuid")) {
                volMeta.set("uuid", vol.stem().string());
            }
            if (!volMeta.hasKey("name")) {
                volMeta.set("name", vol.stem().string());
            }
            if (!volMeta.hasKey("type")) {
                volMeta.set("type", "vol");
            }

            // Save the new metadata
            volMeta.save();
        }
    }

    // Update the version
    auto newMeta = meta;
    newMeta.set("version", 6);
    newMeta.save();

    return newMeta;
}

auto VolpkgV6ToV7(const Metadata& meta) -> Metadata
{
    // Nothing to do check
    if (meta.get<int>("version") != 6) {
        return meta;
    }
    Logger()->info("Performing v7 migrations");

    // VolumePkg path
    const auto path = meta.path().parent_path();

    // Add renders folder
    Logger()->debug("- Adding transforms directory");
    const fs::path tfmsDir = path / "transforms";
    if (not fs::exists(tfmsDir)) {
        fs::create_directory(tfmsDir);
    }

    // Add vc keep files
    Logger()->debug("- Adding keep files");
    for (const auto& d : {"paths", "renders", "volumes", "transforms"}) {
        ::keep(path / d);
    }

    // Update the version
    auto newMeta = meta;
    newMeta.set("version", 7);
    newMeta.save();

    return newMeta;
}

using UpgradeFn = std::function<Metadata(const Metadata&)>;
const std::vector<UpgradeFn> UPGRADE_FNS{
    VolpkgV3ToV4, VolpkgV4ToV5, VolpkgV5ToV6, VolpkgV6ToV7};

}  // namespace

// CONSTRUCTORS //
// Make a volpkg of a particular version number
VolumePkg::VolumePkg(fs::path fileLocation, int version)
    : rootDir_{std::move(fileLocation)}
{
    // Lookup the metadata template from our library of versions
    auto findDict = VERSION_LIBRARY.find(version);
    if (findDict == std::end(VERSION_LIBRARY)) {
        throw std::runtime_error("No dictionary found for volpkg");
    }

    // Create the directories with the default values
    config_ = VolumePkg::InitConfig(findDict->second, version);
    config_.setPath(rootDir_ / ::CONFIG);

    // Make directories
    for (const auto& d : ::ReqDirs(rootDir_)) {
        if (not fs::exists(d)) {
            fs::create_directory(d);
        }
        if (d != rootDir_) {
            ::keep(d);
        }
    }

    // Do initial save
    config_.save();
}

// Use this when reading a volpkg from a file
VolumePkg::VolumePkg(const fs::path& fileLocation) : rootDir_{fileLocation}
{
    // Loads the metadata
    config_ = Metadata(fileLocation / ::CONFIG);

    // Auto-upgrade on load from v
    auto version = config_.get<int>("version");
    if (version >= 6 and version != VOLPKG_VERSION_LATEST) {
        Upgrade(fileLocation, VOLPKG_VERSION_LATEST);
        config_ = Metadata(fileLocation / ::CONFIG);
    }

    // Check directory structure
    for (const auto& d : ::ReqDirs(rootDir_)) {
        if (not fs::exists(d)) {
            Logger()->warn(
                "Creating missing VolumePkg directory: {}",
                d.filename().string());
            fs::create_directory(d);
        }
        if (d != rootDir_) {
            ::keep(d);
        }
    }

    // Load volumes into volumes_
    for (const auto& entry : fs::directory_iterator(::VolsDir(rootDir_))) {
        fs::path dirpath = fs::canonical(entry);
        if (fs::is_directory(dirpath)) {
            auto v = Volume::New(dirpath);
            volumes_.emplace(v->id(), v);
        }
    }

    // Load segmentations from ALL available directories at startup
    auto availableDirs = getAvailableSegmentationDirectories();
    for (const auto& dirName : availableDirs) {
        loadSegmentationsFromDirectory(dirName);
    }
}

auto VolumePkg::New(fs::path fileLocation, int version) -> VolumePkg::Pointer
{
    return std::make_shared<VolumePkg>(fileLocation, version);
}

// Shared pointer volumepkg construction
auto VolumePkg::New(fs::path fileLocation) -> VolumePkg::Pointer
{
    return std::make_shared<VolumePkg>(fileLocation);
}

// METADATA RETRIEVAL //
// Returns Volume Name from JSON config
auto VolumePkg::name() const -> std::string
{
    // Gets the Volume name from the configuration file
    auto name = config_.get<std::string>("name");
    if (name != "NULL") {
        return name;
    }

    return "UnnamedVolume";
}

auto VolumePkg::version() const -> int { return config_.get<int>("version"); }

auto VolumePkg::materialThickness() const -> double
{
    return config_.get<double>("materialthickness");
}

auto VolumePkg::metadata() const -> Metadata { return config_; }

void VolumePkg::saveMetadata() { config_.save(); }

void VolumePkg::saveMetadata(const fs::path& filePath)
{
    config_.save(filePath);
}

// VOLUME FUNCTIONS //
auto VolumePkg::hasVolumes() const -> bool { return !volumes_.empty(); }

auto VolumePkg::hasVolume(const Volume::Identifier& id) const -> bool
{
    return volumes_.count(id) > 0;
}

auto VolumePkg::numberOfVolumes() const -> std::size_t
{
    return volumes_.size();
}

auto VolumePkg::volumeIDs() const -> std::vector<Volume::Identifier>
{
    std::vector<Volume::Identifier> ids;
    for (const auto& v : volumes_) {
        ids.emplace_back(v.first);
    }
    return ids;
}

auto VolumePkg::volumeNames() const -> std::vector<std::string>
{
    std::vector<Volume::Identifier> names;
    for (const auto& v : volumes_) {
        names.emplace_back(v.second->name());
    }
    return names;
}

auto VolumePkg::newVolume(std::string name) -> Volume::Pointer
{
    // Generate a uuid
    auto uuid = DateTime();

    // Get dir name if not specified
    if (name.empty()) {
        name = uuid;
    }

    // Make the volume directory
    auto volDir = ::VolsDir(rootDir_) / uuid;
    if (!fs::exists(volDir)) {
        fs::create_directory(volDir);
    } else {
        throw std::runtime_error("Volume directory already exists");
    }

    // Make the volume
    auto r = volumes_.emplace(uuid, Volume::New(volDir, uuid, name));
    if (!r.second) {
        auto msg = "Volume already exists with ID " + uuid;
        throw std::runtime_error(msg);
    }

    // Return the Volume Pointer
    return r.first->second;
}

auto VolumePkg::volume() const -> const Volume::Pointer
{
    if (volumes_.empty()) {
        throw std::out_of_range("No volumes in VolPkg");
    }
    return volumes_.begin()->second;
}

auto VolumePkg::volume() -> Volume::Pointer
{
    if (volumes_.empty()) {
        throw std::out_of_range("No volumes in VolPkg");
    }
    return volumes_.begin()->second;
}

auto VolumePkg::volume(const Volume::Identifier& id) const
    -> const Volume::Pointer
{
    return volumes_.at(id);
}

auto VolumePkg::volume(const Volume::Identifier& id) -> Volume::Pointer
{
    return volumes_.at(id);
}

// SEGMENTATION FUNCTIONS //
auto VolumePkg::hasSegmentations() const -> bool
{
    return !segmentations_.empty();
}

auto VolumePkg::numberOfSegmentations() const -> std::size_t
{
    return segmentations_.size();
}

auto VolumePkg::segmentation(const DiskBasedObjectBaseClass::Identifier& id)
    const -> const Segmentation::Pointer
{
    return segmentations_.at(id);
}

std::vector<fs::path> VolumePkg::segmentationFiles()
{
    return segmentation_files_;
}

auto VolumePkg::segmentation(const DiskBasedObjectBaseClass::Identifier& id)
    -> Segmentation::Pointer
{
    return segmentations_.at(id);
}

auto VolumePkg::segmentationIDs() const -> std::vector<Segmentation::Identifier>
{
    std::vector<Segmentation::Identifier> ids;
    // Only return IDs from the current directory
    for (const auto& s : segmentations_) {
        auto it = segmentationDirectories_.find(s.first);
        if (it != segmentationDirectories_.end() && it->second == currentSegmentationDir_) {
            ids.emplace_back(s.first);
        }
    }
    return ids;
}

auto VolumePkg::segmentationNames() const -> std::vector<std::string>
{
    std::vector<std::string> names;
    for (const auto& s : segmentations_) {
        names.emplace_back(s.second->name());
    }
    return names;
}

auto VolumePkg::InitConfig(const Dictionary& dict, int version) -> Metadata
{
    Metadata config;

    // Populate the config file with keys from the dictionary
    for (const auto& entry : dict) {
        if (entry.first == "version") {
            config.set("version", version);
            continue;
        }

        // Default values
        switch (entry.second) {
            case DictionaryEntryType::Int:
                config.set(entry.first, int{});
                break;
            case DictionaryEntryType::Double:
                config.set(entry.first, double{});
                break;
            case DictionaryEntryType::String:
                config.set(entry.first, std::string{});
                break;
        }
    }

    return config;
}

////////// Upgrade //////////
void VolumePkg::Upgrade(const fs::path& path, int version, bool force)
{
    // Copy the current metadata
    Metadata meta(path / "config.json");

    // Get current version
    const auto currentVersion = meta.get<int>("version");

    // Don't update for versions < 6 unless forced (those migrations are
    // expensive)
    if (currentVersion < 6 and not force) {
        throw std::runtime_error(
            "Volumepkg version " + std::to_string(currentVersion) +
            " should be upgraded with vc_volpkg_upgrade");
    }

    Logger()->info(
        "Upgrading volpkg version {} to {}", currentVersion, version);

    // Plot path to final version
    // UpgradeFns start at v3->v4
    auto startIdx = currentVersion - 3;
    auto endIdx = version - 3;
    for (auto idx = startIdx; idx < endIdx; idx++) {
        meta = ::UPGRADE_FNS[idx](meta);
    }
    // Save the final metadata
    meta.save();
}

// SEGMENTATION DIRECTORY METHODS //
void VolumePkg::loadSegmentationsFromDirectory(const std::string& dirName)
{
    // DO NOT clear existing segmentations - we keep all directories in memory
    // Only remove segmentations from this specific directory
    std::vector<Segmentation::Identifier> toRemove;
    for (const auto& pair : segmentationDirectories_) {
        if (pair.second == dirName) {
            toRemove.push_back(pair.first);
        }
    }
    
    // Remove old segmentations from this directory
    for (const auto& id : toRemove) {
        segmentations_.erase(id);
        segmentationDirectories_.erase(id);
        
        // Remove from files vector
        auto it = std::remove_if(segmentation_files_.begin(), segmentation_files_.end(),
            [&id, this](const fs::path& path) {
                auto segIt = segmentations_.find(id);
                return segIt != segmentations_.end() && segIt->second->path() == path;
            });
        segmentation_files_.erase(it, segmentation_files_.end());
    }
    
    // Check if directory exists
    const auto segDir = ::SegsDir(rootDir_, dirName);
    if (!fs::exists(segDir)) {
        Logger()->warn("Segmentation directory '{}' does not exist", dirName);
        return;
    }
    
    // Load segmentations from the specified directory
    for (const auto& entry : fs::directory_iterator(segDir)) {
        fs::path dirpath = fs::canonical(entry);
        if (fs::is_directory(dirpath)) {
            try {
                auto s = Segmentation::New(dirpath);
                segmentations_.emplace(s->id(), s);
                segmentation_files_.push_back(dirpath);
                // Track which directory this segmentation came from
                segmentationDirectories_[s->id()] = dirName;
            }
            catch (const std::exception &exc) {
                std::cout << "WARNING: some exception occured, skipping segment dir: " << dirpath << std::endl;
                std::cerr << exc.what();
            }
        }
    }
}

void VolumePkg::setSegmentationDirectory(const std::string& dirName)
{
    // Just change the current directory - all segmentations are already loaded
    currentSegmentationDir_ = dirName;
}

auto VolumePkg::getSegmentationDirectory() const -> std::string
{
    return currentSegmentationDir_;
}

auto VolumePkg::getAvailableSegmentationDirectories() const -> std::vector<std::string>
{
    std::vector<std::string> dirs;
    
    // Check for common segmentation directories
    const std::vector<std::string> commonDirs = {"paths", "traces"};
    for (const auto& dir : commonDirs) {
        if (fs::exists(rootDir_ / dir) && fs::is_directory(rootDir_ / dir)) {
            dirs.push_back(dir);
        }
    }
    
    return dirs;
}

void VolumePkg::removeSegmentation(const Segmentation::Identifier& id)
{
    // Check if segmentation exists
    auto it = segmentations_.find(id);
    if (it == segmentations_.end()) {
        throw std::runtime_error("Segmentation not found: " + id);
    }
    
    // Get the path before removing
    fs::path segPath = it->second->path();
    
    // Remove from internal map
    segmentations_.erase(it);
    
    // Remove from files vector
    auto fileIt = std::find(segmentation_files_.begin(), 
                           segmentation_files_.end(), segPath);
    if (fileIt != segmentation_files_.end()) {
        segmentation_files_.erase(fileIt);
    }
    
    // Delete the physical folder
    if (fs::exists(segPath)) {
        fs::remove_all(segPath);
    }
}

void VolumePkg::refreshSegmentations()
{
    const auto segDir = ::SegsDir(rootDir_, currentSegmentationDir_);
    if (!fs::exists(segDir)) {
        Logger()->warn("Segmentation directory '{}' does not exist", currentSegmentationDir_);
        return;
    }
    
    // Build a set of current segmentation paths on disk for the current directory
    std::set<fs::path> diskPaths;
    for (const auto& entry : fs::directory_iterator(segDir)) {
        fs::path dirpath = fs::canonical(entry);
        if (fs::is_directory(dirpath)) {
            diskPaths.insert(dirpath);
        }
    }
    
    // Build a set of currently loaded segmentation paths for the current directory
    std::set<fs::path> loadedPaths;
    for (const auto& seg : segmentations_) {
        auto dirIt = segmentationDirectories_.find(seg.first);
        if (dirIt != segmentationDirectories_.end() && dirIt->second == currentSegmentationDir_) {
            loadedPaths.insert(seg.second->path());
        }
    }
    
    // Since we're keeping all directories in memory, we only add new segmentations
    // We don't remove any because they might belong to other directories
    
    // Find and add new segmentations (on disk but not in memory)
    for (const auto& diskPath : diskPaths) {
        if (loadedPaths.find(diskPath) == loadedPaths.end()) {
            try {
                auto s = Segmentation::New(diskPath);
                segmentations_.emplace(s->id(), s);
                segmentation_files_.push_back(diskPath);
                segmentationDirectories_[s->id()] = currentSegmentationDir_;
            }
            catch (const std::exception &exc) {
                Logger()->warn("Failed to load segment dir: {} - {}", diskPath.string(), exc.what());
            }
        }
    }
}
