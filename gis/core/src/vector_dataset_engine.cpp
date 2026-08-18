#include "gis/vector_dataset_engine.h"

#include "gis/gdal_runtime.h"
#include "json.hpp"

#include <algorithm>
#include <memory>
#include <string>

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

namespace gis {
namespace {

using json = nlohmann::json;

struct DatasetCloser {
    void operator()(GDALDataset* dataset) const noexcept {
        if (dataset != nullptr) GDALClose(dataset);
    }
};
using DatasetPtr = std::unique_ptr<GDALDataset, DatasetCloser>;

struct GeometryDeleter {
    void operator()(OGRGeometry* geometry) const noexcept {
        OGRGeometryFactory::destroyGeometry(geometry);
    }
};
using GeometryPtr = std::unique_ptr<OGRGeometry, GeometryDeleter>;

struct FeatureDeleter {
    void operator()(OGRFeature* feature) const noexcept {
        OGRFeature::DestroyFeature(feature);
    }
};
using FeaturePtr = std::unique_ptr<OGRFeature, FeatureDeleter>;

DatasetPtr OpenVectorDataset(std::string_view path) {
    const std::string owned(path);
    return DatasetPtr(static_cast<GDALDataset*>(GDALOpenEx(
        owned.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr,
        nullptr)));
}

std::string SpatialReferenceName(const OGRSpatialReference* reference) {
    if (reference == nullptr) return {};
    const char* authority = reference->GetAuthorityName(nullptr);
    const char* code = reference->GetAuthorityCode(nullptr);
    if (authority != nullptr && code != nullptr) {
        return std::string(authority) + ":" + code;
    }
    char* wkt = nullptr;
    if (reference->exportToWkt(&wkt) != OGRERR_NONE || wkt == nullptr) return {};
    std::string result(wkt);
    CPLFree(wkt);
    return result;
}

OGRLayer* SelectLayer(GDALDataset& dataset, std::string_view layer_name) {
    if (layer_name.empty()) return dataset.GetLayer(0);
    const std::string owned(layer_name);
    return dataset.GetLayerByName(owned.c_str());
}

json FeatureJson(const OGRFeature& feature) {
    json properties = json::object();
    const OGRFeatureDefn* definition = feature.GetDefnRef();
    if (definition != nullptr) {
        for (int index = 0; index < definition->GetFieldCount(); ++index) {
            const OGRFieldDefn* field = definition->GetFieldDefn(index);
            if (field == nullptr) continue;
            if (!feature.IsFieldSetAndNotNull(index)) {
                properties[field->GetNameRef()] = nullptr;
                continue;
            }
            switch (field->GetType()) {
                case OFTInteger:
                    properties[field->GetNameRef()] = feature.GetFieldAsInteger(index);
                    break;
                case OFTInteger64:
                    properties[field->GetNameRef()] = feature.GetFieldAsInteger64(index);
                    break;
                case OFTReal:
                    properties[field->GetNameRef()] = feature.GetFieldAsDouble(index);
                    break;
                default:
                    // GDAL's string representation is stable for date/time and
                    // list fields and keeps this API JSON-safe without exposing
                    // OGR's union-valued field storage.
                    properties[field->GetNameRef()] = feature.GetFieldAsString(index);
                    break;
            }
        }
    }

    json geometry = nullptr;
    const OGRGeometry* source_geometry = feature.GetGeometryRef();
    if (source_geometry != nullptr) {
        char* encoded = source_geometry->exportToJson();
        if (encoded != nullptr) {
            geometry = json::parse(encoded);
            CPLFree(encoded);
        }
    }
    json output = {{"type", "Feature"}, {"properties", std::move(properties)},
                   {"geometry", std::move(geometry)}};
    if (feature.GetFID() != OGRNullFID) output["id"] = feature.GetFID();
    return output;
}

}  // namespace

DatasetInfoResult VectorDatasetEngine::Inspect(std::string_view path) const {
    GdalRuntime::EnsureInitialized();
    DatasetInfoResult result;
    DatasetPtr dataset = OpenVectorDataset(path);
    if (!dataset) {
        result.error = "Unable to open vector dataset";
        return result;
    }

    if (dataset->GetDriver() != nullptr) {
        result.driver = dataset->GetDriver()->GetDescription();
    }
    const int layer_count = dataset->GetLayerCount();
    result.layers.reserve(static_cast<std::size_t>(std::max(layer_count, 0)));
    for (int index = 0; index < layer_count; ++index) {
        OGRLayer* layer = dataset->GetLayer(index);
        if (layer == nullptr) continue;

        DatasetLayerInfo info;
        info.name = layer->GetName();
        info.feature_count = layer->GetFeatureCount(false);
        info.crs = SpatialReferenceName(layer->GetSpatialRef());
        OGRFeatureDefn* definition = layer->GetLayerDefn();
        if (definition != nullptr) {
            info.geometry_type = OGRGeometryTypeToName(definition->GetGeomType());
            info.fields.reserve(static_cast<std::size_t>(definition->GetFieldCount()));
            for (int field_index = 0; field_index < definition->GetFieldCount(); ++field_index) {
                const OGRFieldDefn* field = definition->GetFieldDefn(field_index);
                if (field == nullptr) continue;
                info.fields.push_back({
                    .name = field->GetNameRef(),
                    .type = OGRFieldDefn::GetFieldTypeName(field->GetType()),
                    .nullable = field->IsNullable() != 0,
                    .width = field->GetWidth(),
                    .precision = field->GetPrecision(),
                });
            }
        }
        OGREnvelope envelope;
        if (layer->GetExtent(&envelope, false) == OGRERR_NONE) {
            info.extent = DatasetExtent{envelope.MinX, envelope.MinY,
                                        envelope.MaxX, envelope.MaxY};
        }
        result.layers.push_back(std::move(info));
    }
    result.success = true;
    return result;
}

FeaturesWithinResult VectorDatasetEngine::FeaturesWithin(
    std::string_view path, std::string_view polygon_geojson,
    std::string_view layer_name, std::size_t limit) const {
    GdalRuntime::EnsureInitialized();
    FeaturesWithinResult result;
    DatasetPtr dataset = OpenVectorDataset(path);
    if (!dataset) {
        result.error = "Unable to open vector dataset";
        return result;
    }
    OGRLayer* layer = SelectLayer(*dataset, layer_name);
    if (layer == nullptr) {
        result.error = "Vector dataset layer was not found";
        return result;
    }

    const std::string polygon_text(polygon_geojson);
    GeometryPtr polygon(OGRGeometryFactory::createFromGeoJson(polygon_text.c_str()));
    if (!polygon || polygon->IsEmpty() ||
        (wkbFlatten(polygon->getGeometryType()) != wkbPolygon &&
         wkbFlatten(polygon->getGeometryType()) != wkbMultiPolygon)) {
        result.error = "Query geometry must be a non-empty GeoJSON Polygon or MultiPolygon";
        return result;
    }

    // OGR keeps only a borrowed filter reference for the duration of reads;
    // polygon therefore deliberately outlives the complete feature loop.
    layer->SetSpatialFilter(polygon.get());
    layer->ResetReading();
    result.layer_name = layer->GetName();
    result.crs = SpatialReferenceName(layer->GetSpatialRef());

    json features = json::array();
    while (FeaturePtr feature{layer->GetNextFeature()}) {
        if (result.feature_count >= limit) {
            result.truncated = true;
            break;
        }
        features.push_back(FeatureJson(*feature));
        ++result.feature_count;
    }
    layer->SetSpatialFilter(nullptr);
    result.geojson = json{{"type", "FeatureCollection"},
                          {"features", std::move(features)}}.dump();
    result.success = true;
    return result;
}

}  // namespace gis
