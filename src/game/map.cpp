#include "game/map.h"

#include "resource/asset_manager.h"
#include "utils/logger.h"

#include "compiled/warning_off.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include "compiled/warning_on.h"

namespace smallcap
{
namespace
{
std::string ValueToString(const nlohmann::json &value)
{
    if (value.is_string())
    {
        return value.get<std::string>();
    }
    if (value.is_boolean())
    {
        return value.get<Bool>() ? "true" : "false";
    }
    if (value.is_number_integer())
    {
        return std::to_string(value.get<Int64>());
    }
    if (value.is_number())
    {
        return std::to_string(value.get<Double>());
    }
    return value.dump();
}

void ReadProperties(
    const nlohmann::json &node, std::unordered_map<std::string, std::string> &props
)
{
    if (!node.contains("properties"))
    {
        return;
    }
    for (const auto &property : node["properties"])
    {
        props[property["name"].get<std::string>()] = ValueToString(property["value"]);
    }
}

Music MusicFromName(const std::string &name)
{
    if (name == "village")
        return Music::VILLAGE;
    if (name == "woods")
        return Music::WOODS;
    if (name == "tiny")
        return Music::TINY;
    if (name == "dungeon")
        return Music::DUNGEON;
    if (name == "boss")
        return Music::BOSS;
    return Music::NONE;
}
} // namespace

const std::string &MapObject::Prop(const std::string &key) const
{
    static const std::string empty;
    const auto it = props.find(key);
    return it == props.end() ? empty : it->second;
}

Bool LoadTileInfo(TileInfo &info)
{
    auto json =
        resource::AssetManager::GetInstance()->Read<nlohmann::json>(
            resource::AssetPath("game://textures/tileset.json")
        );
    if (!json.has_value())
    {
        LOGERROR("Missing tileset.json");
        return false;
    }
    const nlohmann::json &tileset = *json->Get();
    const auto count = tileset["tilecount"].get<size_t>();
    info.solid.assign(count, 0);
    info.water.assign(count, 0);
    info.animPartner.assign(count, 0);
    for (const auto &[key, props] : tileset["properties"].items())
    {
        const auto id = static_cast<size_t>(std::stoi(key));
        if (id >= count)
        {
            continue;
        }
        info.solid[id] = props.value("solid", false) ? 1 : 0;
        info.water[id] = props.value("water", false) ? 1 : 0;
        if (props.contains("anim"))
        {
            info.animPartner[id] = props["anim"].get<UInt16>();
        }
    }
    return true;
}

Bool LoadMap(const std::string &name, const TileInfo &info, Renderer2D &renderer, Map &out)
{
    auto json = resource::AssetManager::GetInstance()->Read<nlohmann::json>(
        resource::AssetPath("game://maps/" + name + ".json")
    );
    if (!json.has_value())
    {
        LOGERROR("Missing map %s", name.c_str());
        return false;
    }
    const nlohmann::json &data = *json->Get();
    out = Map{};
    out.name = name;
    out.width = data["width"].get<Int32>();
    out.height = data["height"].get<Int32>();
    std::unordered_map<std::string, std::string> map_props;
    ReadProperties(data, map_props);
    out.title = map_props["title"];
    out.music = MusicFromName(map_props["music"]);
    out.rooms = map_props["rooms"] == "true";

    const size_t cells = static_cast<size_t>(out.width * out.height);
    out.ground.assign(cells, 0);
    out.solid.assign(cells, 0);
    for (const auto &layer : data["layers"])
    {
        const std::string type = layer["type"].get<std::string>();
        if (type == "tilelayer")
        {
            const auto &tiles = layer["data"];
            for (size_t i = 0; i < cells && i < tiles.size(); ++i)
            {
                const auto gid = tiles[i].get<UInt32>();
                const UInt16 id = gid == 0 ? 0 : static_cast<UInt16>(gid - 1);
                out.ground[i] = id;
                if (id < info.solid.size() && info.solid[id])
                {
                    out.solid[i] = 1;
                }
            }
        }
        else if (type == "objectgroup")
        {
            for (const auto &object : layer["objects"])
            {
                MapObject o;
                o.id = object.value("id", 0);
                o.type = object.value("type", std::string());
                if (o.type.empty())
                {
                    o.type = object.value("class", std::string());
                }
                o.name = object.value("name", std::string());
                o.x = object.value("x", 0.0f);
                o.y = object.value("y", 0.0f);
                o.w = object.value("width", 0.0f);
                o.h = object.value("height", 0.0f);
                ReadProperties(object, o.props);
                out.objects.push_back(std::move(o));
            }
        }
    }
    out.layer = renderer.BuildTileLayer(out.ground, out.width, out.height, info.animPartner);
    LOGINFO("Loaded map %s (%dx%d, %zu objects)", name.c_str(), out.width, out.height,
            out.objects.size());
    return true;
}

Bool IsSolidTile(const Map &map, const Int32 tx, const Int32 ty)
{
    if (tx < 0 || ty < 0 || tx >= map.width || ty >= map.height)
    {
        return true;
    }
    return map.solid[static_cast<size_t>(ty * map.width + tx)] != 0;
}

Bool BoxHitsTiles(const Map &map, const Float x0, const Float y0, const Float x1, const Float y1)
{
    const auto tx0 = static_cast<Int32>(std::floor(x0 / TILE));
    const auto ty0 = static_cast<Int32>(std::floor(y0 / TILE));
    const auto tx1 = static_cast<Int32>(std::floor((x1 - 0.001f) / TILE));
    const auto ty1 = static_cast<Int32>(std::floor((y1 - 0.001f) / TILE));
    for (Int32 ty = ty0; ty <= ty1; ++ty)
    {
        for (Int32 tx = tx0; tx <= tx1; ++tx)
        {
            if (IsSolidTile(map, tx, ty))
            {
                return true;
            }
        }
    }
    return false;
}
} // namespace smallcap
