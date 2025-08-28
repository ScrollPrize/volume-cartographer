#pragma once

#include <cstddef>
#include <iostream>
#include <map>

#include <filesystem>
#include "vc/core/types/Metadata.hpp"
#include "vc/core/types/Segmentation.hpp"
#include "vc/core/types/Volume.hpp"
#include "vc/core/types/VolumePkgVersion.hpp"

class VolumePkg
{
public:
    VolumePkg(std::filesystem::path fileLocation, int version);
    explicit VolumePkg(const std::filesystem::path& fileLocation);
    static std::shared_ptr<VolumePkg> New(std::filesystem::path fileLocation, int version);
    static std::shared_ptr<VolumePkg> New(std::filesystem::path fileLocation);
    [[nodiscard]] std::string name() const;
    [[nodiscard]] int version() const;
    [[nodiscard]] double materialThickness() const;
    [[nodiscard]] Metadata metadata() const;
    template <typename T>
    void setMetadata(const std::string& key, T value)
    {
        config_.set<T>(key, value);
    }
    void saveMetadata();
    void saveMetadata(const std::filesystem::path& filePath);
    bool hasVolumes() const;
    [[nodiscard]] bool hasVolume(const std::string& id) const;
    auto numberOfVolumes() const -> std::size_t;
    [[nodiscard]] auto volumeIDs() const -> std::vector<std::string>;
    [[nodiscard]] auto volumeNames() const -> std::vector<std::string>;
    auto newVolume(std::string name = "") -> std::shared_ptr<Volume>;
    [[nodiscard]] auto volume() const -> const std::shared_ptr<Volume>;
    auto volume() -> std::shared_ptr<Volume>;
    [[nodiscard]] auto volume(const std::string& id) const -> const std::shared_ptr<Volume>;
    auto volume(const std::string& id) -> std::shared_ptr<Volume>;
    auto hasSegmentations() const -> bool;
    auto numberOfSegmentations() const -> std::size_t;
    [[nodiscard]] auto segmentationIDs() const -> std::vector<std::string>;
    [[nodiscard]] auto segmentationNames() const -> std::vector<std::string>;
    [[nodiscard]] auto segmentation(const std::string& id) const -> const std::shared_ptr<Segmentation>;

    std::vector<std::filesystem::path> segmentationFiles();

    auto segmentation(const std::string& id) -> std::shared_ptr<Segmentation>;
    void removeSegmentation(const std::string& id);
    void setSegmentationDirectory(const std::string& dirName);
    [[nodiscard]] auto getSegmentationDirectory() const -> std::string;
    [[nodiscard]] auto getAvailableSegmentationDirectories() const -> std::vector<std::string>;

    void refreshSegmentations();
    static void Upgrade(
        const std::filesystem::path& path,
        int version = VOLPKG_VERSION_LATEST,
        bool force = false);

private:
    Metadata config_;
    std::filesystem::path rootDir_;
    std::map<std::string, std::shared_ptr<Volume>> volumes_;
    std::map<std::string, std::shared_ptr<Segmentation>> segmentations_;
    std::vector<std::filesystem::path> segmentation_files_;
    std::string currentSegmentationDir_ = "paths";
    std::map<std::string, std::string> segmentationDirectories_;

    static auto InitConfig(const Dictionary& dict, int version) -> Metadata;
    void loadSegmentationsFromDirectory(const std::string& dirName);
};

