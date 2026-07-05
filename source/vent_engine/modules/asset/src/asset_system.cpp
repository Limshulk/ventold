//
// asset module.
// asset manager system implementation.
// ——————————————————————

#include <asset_system.hpp>

#include <_vent/accessors.hpp>
#include <_vent/asset/image.hpp>

#include <core/system/registration.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobjloader/tiny_obj_loader.h>

#include <_vent/asset/model.hpp>

namespace vent {

auto asset_system::initialize() -> bool {

    log()->trace("asset", "initializing asset system...");
    // todo: mount engine assets automatically?
    // we could do this in the launcher, but for now we'll do it manually.

    log()->trace("asset", "initialized asset system.");
    return true;
}

auto asset_system::shutdown() -> void {
    log()->trace("asset", "shutting down asset system...");

    _mount_points.clear();

    log()->trace("asset", "asset shutdown complete.");
}

auto asset_system::mount(std::string_view protocol,
                         std::string_view physical_path) -> void {
    _mount_points[std::string(protocol)] = std::string(physical_path);
    log()->trace("asset", "mounted '{}' -> '{}'.", protocol, physical_path);
}

auto asset_system::resolve(std::string_view virtual_path) const -> std::string {
    log()->trace("asset", "resolving path: '{}'", virtual_path);
    const size_t pos = virtual_path.find("://");
    if (pos == std::string_view::npos) {
        return std::string(virtual_path);
    }

    const std::string_view protocol = virtual_path.substr(0, pos + 3);
    const std::string_view rest     = virtual_path.substr(pos + 3);

    auto it = _mount_points.find(std::string(protocol));
    if (it != _mount_points.end()) {
        std::filesystem::path resolved =
            std::filesystem::path(it->second) / rest;
        return resolved.string();
    }

    log()->error("asset", "unrecognized protocol in path: '{}'.", virtual_path);
    return std::string(virtual_path);
}

auto asset_system::read_binary_file(std::string_view virtual_path) const
    -> std::vector<u8> {
    std::string physical_path = resolve(virtual_path);

    std::ifstream file(physical_path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        log()->error("asset", "failed to open file '{}'", physical_path);
        return {};
    }

    size_t          file_size = static_cast<size_t>(file.tellg());
    std::vector<u8> buffer(file_size);

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    file.close();

    return buffer;
}

auto asset_system::load_shader(std::string_view virtual_path) -> shader_asset* {

    std::string path_str(virtual_path);
    auto        it = _shader_cache.find(path_str);
    if (it != _shader_cache.end()) {
        log()->trace("asset", "shader '{}' found in cache.", virtual_path);
        return it->second.get();
    }

    log()->trace("asset",
                 "shader '{}' not in cache, reading from disk...",
                 virtual_path);

    std::vector<u8> bytecode = read_binary_file(virtual_path);
    if (bytecode.empty()) {
        log()->debug("asset", "bytecode empty for shader '{}'", virtual_path);
        return nullptr;
    }

    auto asset = std::make_unique<shader_asset>();

    // copy u8 vector to u32 vector. sizes must be a multiple of 4 (spirv
    // requirement).
    if (bytecode.size() % sizeof(u32) != 0) {
        log()->error("asset",
                     "shader '{}' size is not a multiple of 4 bytes.",
                     virtual_path);
        return nullptr;
    }

    asset->spirv_bytecode.resize(bytecode.size() / sizeof(u32));
    std::memcpy(asset->spirv_bytecode.data(), bytecode.data(), bytecode.size());

    shader_asset* ptr       = asset.get();
    _shader_cache[path_str] = std::move(asset);

    log()->trace(
        "asset", "shader '{}' successfully loaded and cached.", virtual_path);

    return ptr;
}

auto asset_system::release_shader(shader_asset* asset) -> void {
    if (!asset)
        return;

    // find the asset in the cache and remove it.
    log()->trace("asset", "releasing shader asset.");
    for (auto it = _shader_cache.begin(); it != _shader_cache.end(); ++it) {
        if (it->second.get() == asset) {
            log()->trace("asset", "shader asset found in cache, releasing it.");
            _shader_cache.erase(it);
            return;
        }
    }
}

auto asset_system::load_image(std::string_view virtual_path) -> image_asset* {
    std::string path {virtual_path};
    {
        std::lock_guard lock(_image_mutex);
        if (auto it = _image_cache.find(path); it != _image_cache.end()) {
            return it->second.get();
        }
    }

    std::vector<u8> buffer = read_binary_file(virtual_path);
    if (buffer.empty()) {
        log()->error("asset", "failed to read image file: {}", virtual_path);
        return nullptr;
    }

    int      width, height, channels;
    stbi_uc* pixels = stbi_load_from_memory(buffer.data(),
                                            static_cast<int>(buffer.size()),
                                            &width,
                                            &height,
                                            &channels,
                                            STBI_rgb_alpha);
    if (!pixels) {
        log()->error("asset", "failed to decode image: {}", virtual_path);
        return nullptr;
    }

    auto asset      = std::make_unique<image_asset>();
    asset->width    = width;
    asset->height   = height;
    asset->channels = 4;  // forced STBI_rgb_alpha

    size_t image_size = width * height * 4;
    asset->pixels.resize(image_size);
    std::memcpy(asset->pixels.data(), pixels, image_size);

    stbi_image_free(pixels);

    std::lock_guard lock(_image_mutex);
    _image_cache[path] = std::move(asset);
    log()->trace(
        "asset", "loaded image '{}' ({}x{})", virtual_path, width, height);
    return _image_cache[path].get();
}

auto asset_system::release_image(image_asset* asset) -> void {
    if (!asset)
        return;

    std::lock_guard lock(_image_mutex);
    for (auto it = _image_cache.begin(); it != _image_cache.end(); ++it) {
        if (it->second.get() == asset) {
            log()->trace("asset", "released image '{}'", it->first);
            _image_cache.erase(it);
            return;
        }
    }
}

auto asset_system::load_model(std::string_view virtual_path) -> model_asset* {
    std::string path(virtual_path);

    {
        std::lock_guard lock(_model_mutex);
        if (_model_cache.contains(path)) {
            return _model_cache[path].get();
        }
    }

    std::string phys_path = resolve(virtual_path);
    if (phys_path.empty()) {
        log()->error("asset", "failed to resolve model path: {}", virtual_path);
        return nullptr;
    }

    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string                      warn, err;

    if (!tinyobj::LoadObj(
            &attrib, &shapes, &materials, &warn, &err, phys_path.c_str())) {
        log()->error(
            "asset", "failed to load model: {} ({})", virtual_path, err);
        return nullptr;
    }

    if (!warn.empty()) {
        log()->warn("asset", "tinyobjloader warning: {}", warn);
    }

    auto                            asset = std::make_unique<model_asset>();
    std::unordered_map<vertex, u32> unique_vertices {};

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            vertex vertex {};

            vertex.position = {attrib.vertices[3 * index.vertex_index + 0],
                               attrib.vertices[3 * index.vertex_index + 1],
                               attrib.vertices[3 * index.vertex_index + 2]};

            if (index.texcoord_index >= 0) {
                vertex.u = attrib.texcoords[2 * index.texcoord_index + 0];
                // Vulkan uses 0 at top and 1 at bottom, OBJ uses 0 at bottom
                // and 1 at top
                vertex.v =
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1];
            }

            vertex.color = {1.0f, 1.0f, 1.0f};

            if (!unique_vertices.contains(vertex)) {
                unique_vertices[vertex] =
                    static_cast<u32>(asset->vertices.size());
                asset->vertices.push_back(vertex);
            }

            asset->indices.push_back(unique_vertices[vertex]);
        }
    }

    std::lock_guard lock(_model_mutex);
    _model_cache[path] = std::move(asset);
    log()->trace("asset",
                 "loaded model '{}' ({} vertices, {} indices)",
                 virtual_path,
                 _model_cache[path]->vertices.size(),
                 _model_cache[path]->indices.size());
    return _model_cache[path].get();
}

auto asset_system::release_model(model_asset* asset) -> void {
    if (!asset)
        return;

    std::lock_guard lock(_model_mutex);
    for (auto it = _model_cache.begin(); it != _model_cache.end(); ++it) {
        if (it->second.get() == asset) {
            log()->trace("asset", "released model '{}'", it->first);
            _model_cache.erase(it);
            return;
        }
    }
}

}  // namespace vent

// system registration.
VENT_REGISTER_SYSTEM(vent::asset_system, vent::i_asset, vent::ic_asset);