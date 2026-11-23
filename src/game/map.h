#pragma once
#include "audio/sound.h"
#include "render/renderer2d.h"

#include "compiled/warning_off.h"
#include <string>
#include <unordered_map>
#include <vector>
#include "compiled/warning_on.h"

namespace smallcap
{
struct MapObject
{
    Int32 id = 0;
    std::string type;
    std::string name;
    Float x = 0.0f;
    Float y = 0.0f;
    Float w = 0.0f;
    Float h = 0.0f;
    std::unordered_map<std::string, std::string> props;

    const std::string &Prop(const std::string &key) const;
    Bool Has(const std::string &key) const
    {
        return props.count(key) != 0;
    }
};

struct TileInfo
{
    std::vector<UInt8> solid;
    std::vector<UInt8> water;
    std::vector<UInt16> animPartner;
};

struct Map
{
    std::string name;
    std::string title;
    Int32 width = 0;
    Int32 height = 0;
    std::vector<UInt16> ground;
    std::vector<UInt8> solid;
    std::vector<MapObject> objects;
    Music music = Music::NONE;
    // Dungeon maps are a grid of screen-sized rooms; the camera snaps to the current room.
    Bool rooms = false;
    TileLayerMesh layer;

    Float PixelWidth() const
    {
        return static_cast<Float>(width * TILE);
    }
    Float PixelHeight() const
    {
        return static_cast<Float>(height * TILE);
    }
};

Bool LoadTileInfo(TileInfo &info);
Bool LoadMap(const std::string &name, const TileInfo &info, Renderer2D &renderer, Map &out);
Bool IsSolidTile(const Map &map, Int32 tx, Int32 ty);
// True if the box [x0,x1)x[y0,y1) (game pixels) overlaps a solid tile or leaves the map.
Bool BoxHitsTiles(const Map &map, Float x0, Float y0, Float x1, Float y1);
} // namespace smallcap
