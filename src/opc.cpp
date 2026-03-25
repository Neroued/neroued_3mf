// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#include "opc.h"

#include "neroued/3mf/error.h"

#include "internal/xml_util.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace neroued_3mf::detail {

namespace {

constexpr std::string_view kModelRelType =
    "http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel";
constexpr std::string_view kModelContentType =
    "application/vnd.ms-package.3dmanufacturing-3dmodel+xml";
constexpr std::string_view kRelationshipContentType =
    "application/vnd.openxmlformats-package.relationships+xml";
constexpr std::string_view kProductionNs =
    "http://schemas.microsoft.com/3dmanufacturing/production/2015/06";
constexpr std::string_view kThumbnailRelType =
    "http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail";

std::string NormalizeZipPath(const std::string &path) {
    if (path.empty()) { throw InputError("OPC part path is empty"); }
    std::string n = path;
    std::replace(n.begin(), n.end(), '\\', '/');
    while (!n.empty() && n.front() == '/') { n.erase(0, 1); }
    if (n.empty()) { throw InputError("OPC part path is empty after normalization"); }
    return n;
}

std::string NormalizePartName(const std::string &part_name) {
    return "/" + NormalizeZipPath(part_name);
}

std::string NormalizeExtension(std::string ext) {
    while (!ext.empty() && ext.front() == '.') { ext.erase(0, 1); }
    if (ext.empty()) { throw InputError("Content type extension is empty"); }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string NormalizeRelSource(const std::string &source) {
    if (source.empty() || source == "/") { return {}; }
    return NormalizeZipPath(source);
}

std::string RelPartPathForSource(const std::string &source) {
    if (source.empty()) { return "_rels/.rels"; }
    std::string s = NormalizeZipPath(source);
    auto slash = s.rfind('/');
    if (slash == std::string::npos) { return "_rels/" + s + ".rels"; }
    return s.substr(0, slash) + "/_rels/" + s.substr(slash + 1) + ".rels";
}

template <typename Map>
void InsertOrValidate(Map &entries, const std::string &key, const std::string &ct,
                      const std::string &kind) {
    if (key.empty()) { throw InputError(kind + " key is empty"); }
    if (ct.empty()) { throw InputError(kind + " content type is empty"); }
    auto [it, ok] = entries.emplace(key, ct);
    if (!ok && it->second != ct) { throw InputError("Conflicting OPC " + kind + " for " + key); }
}

std::string BuildRelationshipsXml(const std::vector<OpcRelationship> &rels) {
    std::string xml;
    xml.reserve(256 + rels.size() * 128);
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<Relationships "
           "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
    for (const auto &rel : rels) {
        xml += "  <Relationship Target=\"";
        xml += EscapeXml(rel.target);
        xml += "\" Id=\"";
        xml += EscapeXml(rel.id);
        xml += "\" Type=\"";
        xml += EscapeXml(rel.type);
        xml += "\"/>\n";
    }
    xml += "</Relationships>\n";
    return xml;
}

std::string BuildContentTypesXml(const std::map<std::string, std::string> &defaults,
                                 const std::map<std::string, std::string> &overrides) {
    std::string xml;
    xml.reserve(256 + (defaults.size() + overrides.size()) * 128);
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<Types "
           "xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n";
    for (const auto &[ext, ct] : defaults) {
        xml += "  <Default Extension=\"";
        xml += EscapeXml(ext);
        xml += "\" ContentType=\"";
        xml += EscapeXml(ct);
        xml += "\"/>\n";
    }
    for (const auto &[pn, ct] : overrides) {
        xml += "  <Override PartName=\"";
        xml += EscapeXml(pn);
        xml += "\" ContentType=\"";
        xml += EscapeXml(ct);
        xml += "\"/>\n";
    }
    xml += "</Types>\n";
    return xml;
}

std::vector<uint8_t> StringToBytes(const std::string &s) {
    return {s.begin(), s.end()};
}

std::string ThumbnailExtension(const std::string &content_type) {
    if (content_type == "image/png") { return "png"; }
    if (content_type == "image/jpeg") { return "jpeg"; }
    return "png";
}

} // namespace

std::vector<OpcPart> BuildOpcParts(const Document &doc) {
    if (doc.objects.empty() || doc.build_items.empty()) {
        throw InputError("3MF document has no mesh/build items");
    }

    std::vector<OpcPart> parts;
    parts.reserve(8 + doc.custom_parts.size());

    const std::string model_path = "3D/3dmodel.model";

    // -- relationships by source_part ---------------------------------------
    std::unordered_map<std::string, std::vector<OpcRelationship>> rels_by_source;
    rels_by_source[""] = {
        {.id = "rel0", .type = std::string(kModelRelType), .target = "/" + model_path}};

    // -- thumbnail ----------------------------------------------------------
    std::string thumbnail_path;
    if (doc.thumbnail.has_value() && !doc.thumbnail->data.empty()) {
        std::string ext = ThumbnailExtension(doc.thumbnail->content_type);
        thumbnail_path = "Metadata/thumbnail." + ext;
        parts.push_back({thumbnail_path, doc.thumbnail->content_type, doc.thumbnail->data});
        rels_by_source[""].push_back({.id = "rel-thumb",
                                      .type = std::string(kThumbnailRelType),
                                      .target = "/" + thumbnail_path});
    }

    for (const auto &cr : doc.custom_relationships) {
        std::string src = NormalizeRelSource(cr.source_part);
        rels_by_source[src].push_back({cr.id, cr.type, cr.target});
    }

    // -- collect content types for all known parts --------------------------
    std::unordered_map<std::string, std::string> part_content_types;
    part_content_types.emplace(model_path, std::string(kModelContentType));
    if (!thumbnail_path.empty()) {
        part_content_types.emplace(thumbnail_path, doc.thumbnail->content_type);
    }

    // -- custom parts -------------------------------------------------------
    for (const auto &cp : doc.custom_parts) {
        std::string normalized = NormalizeZipPath(cp.path_in_zip);
        parts.push_back({normalized, cp.content_type, cp.data});
        if (!cp.content_type.empty()) { part_content_types.emplace(normalized, cp.content_type); }
    }

    // -- production extension: assembly model + external object refs ---------
    if (doc.production.enabled) {
        std::unordered_set<std::string> reserved_prefixes;
        reserved_prefixes.insert("p");

        std::string axml;
        axml.reserve(4096);
        axml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<model unit=\"";
        axml += UnitToStringView(doc.unit);
        axml += "\" xml:lang=\"";
        axml += EscapeXml(doc.language);
        axml += "\" xmlns=\"";
        axml += kCoreNs;
        axml += "\" xmlns:p=\"";
        axml += kProductionNs;
        axml += "\"";
        for (const auto &ns : doc.custom_namespaces) {
            if (ns.prefix.empty() || reserved_prefixes.contains(ns.prefix)) { continue; }
            axml += " xmlns:";
            axml += EscapeXml(ns.prefix);
            axml += "=\"";
            axml += EscapeXml(ns.uri);
            axml += "\"";
        }
        axml += " requiredextensions=\"p\">\n";

        for (const auto &md : doc.metadata) {
            if (md.name.empty()) { continue; }
            axml += "  <metadata name=\"";
            axml += EscapeXml(md.name);
            axml += "\"";
            if (!md.type.empty()) {
                axml += " type=\"";
                axml += EscapeXml(md.type);
                axml += "\"";
            }
            axml += ">";
            axml += EscapeXml(md.value);
            axml += "</metadata>\n";
        }

        uint32_t assembly_id = 0;
        for (const auto &obj : doc.objects) { assembly_id = std::max(assembly_id, obj.id); }
        ++assembly_id;

        axml += "  <resources>\n    <object id=\"";
        axml += std::to_string(assembly_id);
        axml += "\" type=\"model\">\n      <components>\n";

        const bool merge = doc.production.merge_objects;
        const std::string merged_ext_path = "/3D/Objects/object_1.model";

        for (std::size_t i = 0; i < doc.objects.size(); ++i) {
            const auto &obj = doc.objects[i];
            std::string ext_path =
                merge ? merged_ext_path : "/3D/Objects/object_" + std::to_string(i + 1) + ".model";
            axml += "        <component p:path=\"";
            axml += EscapeXml(ext_path);
            axml += "\" objectid=\"";
            axml += std::to_string(obj.id);
            axml += "\"";
            if (!obj.component_transform.IsIdentity()) {
                axml += " transform=\"";
                axml += SerializeTransform(obj.component_transform);
                axml += "\"";
            }
            if (!obj.uuid.empty()) {
                axml += " p:UUID=\"";
                axml += EscapeXml(obj.uuid);
                axml += "\"";
            }
            axml += "/>\n";
        }

        axml +=
            "      </components>\n    </object>\n  </resources>\n  <build>\n    <item objectid=\"";
        axml += std::to_string(assembly_id);
        axml += "\"";
        if (!doc.production.assembly_build_transform.IsIdentity()) {
            axml += " transform=\"";
            axml += SerializeTransform(doc.production.assembly_build_transform);
            axml += "\"";
        }
        axml += " printable=\"1\"/>\n  </build>\n</model>\n";

        parts.push_back({model_path, std::string(kModelContentType), StringToBytes(axml)});

        if (merge) {
            std::string ext_zip = "3D/Objects/object_1.model";
            part_content_types.emplace(ext_zip, std::string(kModelContentType));
            rels_by_source[model_path].push_back(
                {.id = "rel-obj-1", .type = std::string(kModelRelType), .target = "/" + ext_zip});
        } else {
            for (std::size_t i = 0; i < doc.objects.size(); ++i) {
                std::string ext_path = "3D/Objects/object_" + std::to_string(i + 1) + ".model";
                part_content_types.emplace(ext_path, std::string(kModelContentType));
                rels_by_source[model_path].push_back({.id = "rel-obj-" + std::to_string(i + 1),
                                                      .type = std::string(kModelRelType),
                                                      .target = "/" + ext_path});
            }
        }
    }

    // -- relationship parts (sorted for determinism) ------------------------
    std::vector<std::string> source_paths;
    source_paths.reserve(rels_by_source.size());
    for (const auto &[key, _] : rels_by_source) { source_paths.push_back(key); }
    std::sort(source_paths.begin(), source_paths.end());

    for (const auto &src : source_paths) {
        auto rels = std::move(rels_by_source[src]);
        std::sort(rels.begin(), rels.end(),
                  [](const OpcRelationship &a, const OpcRelationship &b) { return a.id < b.id; });
        parts.push_back({RelPartPathForSource(src), std::string(kRelationshipContentType),
                         StringToBytes(BuildRelationshipsXml(rels))});
    }

    // -- [Content_Types].xml ------------------------------------------------
    std::map<std::string, std::string> defaults;
    std::map<std::string, std::string> overrides;
    InsertOrValidate(defaults, "rels", std::string(kRelationshipContentType), "default");
    InsertOrValidate(defaults, "model", std::string(kModelContentType), "default");

    for (const auto &[p, ct] : part_content_types) {
        InsertOrValidate(overrides, NormalizePartName(p), ct, "override");
    }
    for (const auto &cct : doc.custom_content_types) {
        InsertOrValidate(defaults, NormalizeExtension(cct.extension), cct.content_type, "default");
    }

    parts.push_back({"[Content_Types].xml", "application/xml",
                     StringToBytes(BuildContentTypesXml(defaults, overrides))});
    return parts;
}

} // namespace neroued_3mf::detail
