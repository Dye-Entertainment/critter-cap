#pragma once
#include "audio/sound.h"
#include "render/renderer2d.h"

#include "compiled/warning_off.h"
#include <string>
#include <unordered_map>
#include <vector>
#include "compiled/warning_on.h"

namespace crittercap
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
    // Tall grass that hides wild critters.
    std::vector<UInt8> grass;
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
    std::vector<UInt8> grass;
    std::vector<MapObject> objects;
    // Wild critter table ("blob_g:3-5:40,ant:4-6:20") and where they hide: in tall grass, or
    // anywhere on the map (caves).
    std::string encounters;
    Bool wildEverywhere = false;
    // Healing spot used after a wipe-out, if this map has one.
    std::string healSpawn;
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
Bool IsGrassTile(const Map &map, Int32 tx, Int32 ty);
// True if the box [x0,x1)x[y0,y1) (game pixels) overlaps a solid tile or leaves the map.
Bool BoxHitsTiles(const Map &map, Float x0, Float y0, Float x1, Float y1);
} // namespace crittercap
