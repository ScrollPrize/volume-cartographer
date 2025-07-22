#include "VCCollection.hpp"
#include <QDebug>
#include <algorithm>
#include <vector>

namespace ChaoVis
{

VCCollection::VCCollection(QObject* parent)
    : QObject(parent)
{
}

VCCollection::~VCCollection() = default;

uint64_t VCCollection::addCollection(const std::string& name)
{
    return findOrCreateCollectionByName(name);
}

ColPoint VCCollection::addPoint(const std::string& collectionName, const cv::Vec3f& point)
{
    uint64_t collection_id = findOrCreateCollectionByName(collectionName);
    
    ColPoint new_point;
    new_point.id = getNextPointId();
    new_point.collectionId = collection_id;
    new_point.p = point;
    
    _collections[collection_id].points[new_point.id] = new_point;
    _points[new_point.id] = new_point;
    
    emit pointAdded(new_point);
    return new_point;
}

void VCCollection::addPoints(const std::string& collectionName, const std::vector<cv::Vec3f>& points)
{
    uint64_t collection_id = findOrCreateCollectionByName(collectionName);
    auto& collection_points = _collections[collection_id].points;

    for (const auto& p : points) {
        ColPoint new_point;
        new_point.id = getNextPointId();
        new_point.collectionId = collection_id;
        new_point.p = p;
        collection_points[new_point.id] = new_point;
        _points[new_point.id] = new_point;
        emit pointAdded(new_point);
    }
}

void VCCollection::updatePoint(const ColPoint& point)
{
    if (_points.count(point.id)) {
        _points[point.id] = point;
        if (_collections.count(point.collectionId)) {
            _collections.at(point.collectionId).points[point.id] = point;
        }
        emit pointChanged(point);
    }
}

void VCCollection::removePoint(uint64_t pointId)
{
    if (_points.count(pointId)) {
        uint64_t collection_id = _points.at(pointId).collectionId;
        _points.erase(pointId);
        if (_collections.count(collection_id)) {
            _collections.at(collection_id).points.erase(pointId);
        }
        emit pointRemoved(pointId);
    }
}

void VCCollection::clearCollection(uint64_t collectionId)
{
    if (_collections.count(collectionId)) {
        auto& collection = _collections.at(collectionId);
        for (const auto& pair : collection.points) {
            _points.erase(pair.first);
            emit pointRemoved(pair.first);
        }
        _collections.erase(collectionId);
        emit collectionRemoved(collectionId);
    }
}

void VCCollection::clearAll()
{
    for (auto& point_pair : _points) {
        emit pointRemoved(point_pair.first);
    }
    _collections.clear();
    _points.clear();
    emit collectionRemoved(-1); // Sentinel for "all removed"
}

void VCCollection::renameCollection(uint64_t collectionId, const std::string& newName)
{
    if (_collections.count(collectionId)) {
        _collections.at(collectionId).name = newName;
        emit collectionChanged(collectionId);
    }
}

uint64_t VCCollection::getCollectionId(const std::string& name) const
{
    auto it = findCollectionByName(name);
    return it.has_value() ? it.value() : 0;
}

const std::unordered_map<uint64_t, VCCollection::Collection>& VCCollection::getAllCollections() const
{
    return _collections;
}

void VCCollection::setCollectionMetadata(uint64_t collectionId, const CollectionMetadata& metadata)
{
    if (_collections.count(collectionId)) {
        _collections.at(collectionId).metadata = metadata;
        emit collectionChanged(collectionId);
    }
}

void VCCollection::setCollectionColor(uint64_t collectionId, const cv::Vec3f& color)
{
    if (_collections.count(collectionId)) {
        _collections.at(collectionId).color = color;
        emit collectionChanged(collectionId);
    }
}

std::optional<ColPoint> VCCollection::getPoint(uint64_t pointId) const
{
    if (_points.count(pointId)) {
        return _points.at(pointId);
    }
    return std::nullopt;
}

std::vector<ColPoint> VCCollection::getPoints(const std::string& collectionName) const
{
    std::vector<ColPoint> points;
    auto collection_id_opt = findCollectionByName(collectionName);
    if (collection_id_opt) {
        const auto& collection = _collections.at(*collection_id_opt);
        for (const auto& pair : collection.points) {
            points.push_back(pair.second);
        }
    }
    return points;
}

std::string VCCollection::generateNewCollectionName(const std::string& prefix) const
{
    int i = 1;
    std::string new_name;
    do {
        new_name = prefix + std::to_string(i++);
        bool name_exists = false;
        for(const auto& pair : _collections) {
            if (pair.second.name == new_name) {
                name_exists = true;
                break;
            }
        }
        if (!name_exists) break;
    } while (true);
    return new_name;
}

void VCCollection::autoFillWindingNumbers(uint64_t collectionId)
{
    if (_collections.count(collectionId)) {
        auto& collection = _collections.at(collectionId);
        
        std::vector<ColPoint*> points_to_sort;
        for(auto& pair : collection.points) {
            points_to_sort.push_back(&pair.second);
        }

        std::sort(points_to_sort.begin(), points_to_sort.end(),
            [](const ColPoint* a, const ColPoint* b) {
                return a->id < b->id;
            });

        float winding_counter = 1.0f;
        for(ColPoint* point : points_to_sort) {
            point->winding_annotation = winding_counter;
            updatePoint(*point);
            winding_counter += 1.0f;
        }
    }
}

uint64_t VCCollection::getNextPointId()
{
    return _next_point_id++;
}

uint64_t VCCollection::getNextCollectionId()
{
    return _next_collection_id++;
}

std::optional<uint64_t> VCCollection::findCollectionByName(const std::string& name) const
{
    for (const auto& pair : _collections) {
        if (pair.second.name == name) {
            return pair.first;
        }
    }
    return std::nullopt;
}

uint64_t VCCollection::findOrCreateCollectionByName(const std::string& name)
{
    auto existing_id = findCollectionByName(name);
    if (existing_id) {
        return *existing_id;
    }

    uint64_t new_id = getNextCollectionId();
    cv::Vec3f color = {
        (float)rand() / RAND_MAX,
        (float)rand() / RAND_MAX,
        (float)rand() / RAND_MAX
    };
    _collections[new_id] = {new_id, name, {}, {}, color};
    emit collectionAdded(new_id);
    return new_id;
}

} // namespace ChaoVis