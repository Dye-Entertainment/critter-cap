#!/usr/bin/env python3
"""Builds Small Cap's maps and writes them as Tiled JSON (.json maps + tiles.tsj tileset).

Usage: make_maps.py <out_dir>   (reads ../textures/tileset.json next to out_dir)
The maps open in the Tiled editor; the game reads the same files.
"""
import json
import os
import random
import sys

T = 16


class MapBuilder:
    def __init__(self, name, w, h, base, title, music, rooms=False, seed=1):
        self.name, self.w, self.h = name, w, h
        self.title, self.music, self.rooms = title, music, rooms
        self.rng = random.Random(seed)
        self.base = base
        self.tiles = [[None] * w for _ in range(h)]
        self.dirt = [[False] * w for _ in range(h)]
        self.water = [[False] * w for _ in range(h)]
        self.objects = []
        self.next_id = 1

    # -- tiles ------------------------------------------------------------------------------------
    def inside(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h

    def set(self, x, y, name):
        if self.inside(x, y):
            self.tiles[y][x] = name
            self.dirt[y][x] = False
            self.water[y][x] = False

    def fill(self, x, y, w, h, name):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, name)

    def path(self, x, y, w, h):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if self.inside(xx, yy):
                    self.tiles[yy][xx] = None
                    self.dirt[yy][xx] = True

    def pond(self, x, y, w, h, round_corners=True):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                corner = (xx in (x, x + w - 1)) and (yy in (y, y + h - 1))
                if round_corners and corner:
                    continue
                if self.inside(xx, yy):
                    self.tiles[yy][xx] = None
                    self.water[yy][xx] = True

    def stamp2x2(self, prefix, x, y):
        for i, part in enumerate(("tl", "tr", "bl", "br")):
            self.set(x + i % 2, y + i // 2, "%s_%s" % (prefix, part))

    def tree(self, x, y):
        self.stamp2x2("tree", x, y)

    def blade(self, x, y, kind="blade"):
        self.set(x, y, kind + "_t")
        self.set(x, y + 1, kind + "_b")

    def house(self, x, y):
        rows = [("roof_tl", "roof_t", "roof_t", "roof_t", "roof_tr"),
                ("roof_ml", "roof_m", "roof_m", "roof_m", "roof_mr"),
                ("roof_bl", "roof_b", "roof_b", "roof_b", "roof_br"),
                ("wall_l", "wall_window", "wall_door", "wall_window", "wall_r")]
        for dy, row in enumerate(rows):
            for dx, name in enumerate(row):
                self.set(x + dx, y + dy, name)

    def free(self, x, y):
        return self.inside(x, y) and self.tiles[y][x] is None and not self.water[y][x]

    # -- objects ----------------------------------------------------------------------------------
    def obj(self, otype, x, y, w=1, h=1, name="", **props):
        o = {"id": self.next_id, "name": name, "type": otype, "x": x * T, "y": y * T,
             "width": w * T, "height": h * T, "rotation": 0, "visible": True}
        if props:
            o["properties"] = []
            for k, v in props.items():
                t = "bool" if isinstance(v, bool) else ("int" if isinstance(v, int) else "string")
                o["properties"].append({"name": k, "type": t, "value": v})
        self.next_id += 1
        self.objects.append(o)
        return o

    def spawn(self, name, x, y, direction="down"):
        return self.obj("spawn", x, y, name=name, dir=direction)

    # -- export -----------------------------------------------------------------------------------
    def export(self, out_dir, tileset):
        names = tileset["names"]
        data = []
        for y in range(self.h):
            for x in range(self.w):
                if self.water[y][x]:
                    m = self.mask(self.water, x, y, edge=True)
                    name = "water_%d_0" % m
                elif self.dirt[y][x]:
                    name = "dirt_%d" % self.mask(self.dirt, x, y, edge=True)
                else:
                    name = self.tiles[y][x]
                    if name is None:
                        name = self.base(x, y, self.rng)
                data.append(names[name] + 1)
        doc = {
            "type": "map", "version": "1.10", "tiledversion": "1.10.2",
            "orientation": "orthogonal", "renderorder": "right-down",
            "width": self.w, "height": self.h, "tilewidth": T, "tileheight": T,
            "infinite": False, "nextlayerid": 3, "nextobjectid": self.next_id,
            "properties": [
                {"name": "title", "type": "string", "value": self.title},
                {"name": "music", "type": "string", "value": self.music},
                {"name": "rooms", "type": "bool", "value": self.rooms},
            ],
            "tilesets": [{"firstgid": 1, "source": "../textures/tiles.tsj"}],
            "layers": [
                {"type": "tilelayer", "name": "ground", "id": 1, "width": self.w,
                 "height": self.h, "x": 0, "y": 0, "opacity": 1, "visible": True, "data": data},
                {"type": "objectgroup", "name": "objects", "id": 2, "x": 0, "y": 0,
                 "opacity": 1, "visible": True, "draworder": "topdown", "objects": self.objects},
            ],
        }
        with open(os.path.join(out_dir, self.name + ".json"), "w") as f:
            json.dump(doc, f, separators=(",", ":"))
        print("%s: %dx%d, %d objects" % (self.name, self.w, self.h, len(self.objects)))

    def mask(self, grid, x, y, edge):
        def same(xx, yy):
            if not self.inside(xx, yy):
                return edge
            return grid[yy][xx]
        m = 0
        if same(x, y - 1):
            m |= 1
        if same(x + 1, y):
            m |= 2
        if same(x, y + 1):
            m |= 4
        if same(x - 1, y):
            m |= 8
        return m


def grass_base(x, y, rng):
    r = rng.random()
    if r < 0.04:
        return rng.choice(("flowers_r", "flowers_y", "flowers_w"))
    if r < 0.35:
        return "grass2"
    if r < 0.5:
        return "grass3"
    return "grass"


def soil_base(x, y, rng):
    r = rng.random()
    if r < 0.06:
        return "clover"
    if r < 0.4:
        return "soil2"
    if r < 0.55:
        return "soil3"
    return "soil"


# =============================================================================================
# Hearthwood village
# =============================================================================================
def build_village():
    m = MapBuilder("village", 40, 26, grass_base, "Hearthwood", "village", seed=11)
    for x in range(0, 40, 2):
        m.tree(x, 0)
        m.tree(x, 24)
    for y in range(2, 24, 2):
        m.tree(0, y)
        if y != 12:
            m.tree(38, y)
    # The east exit is one tile wide; a rock fills the lower half of the gap.
    m.set(38, 13, "rock")
    m.set(39, 13, "rock")

    m.path(2, 12, 37, 2)
    m.path(38, 12, 2, 1)
    m.fill(16, 9, 8, 8, "plaza")
    # Houses and their paths.
    for hx, hy in ((4, 4), (11, 4), (26, 4)):
        m.house(hx, hy)
        m.path(hx + 2, hy + 4, 1, 12 - (hy + 4))
    m.house(5, 16)
    m.path(7, 20, 3, 1)
    m.path(9, 14, 1, 7)
    m.house(28, 16)
    m.path(30, 20, 3, 1)
    m.path(32, 14, 1, 7)
    # Pond with flowers.
    m.pond(31, 5, 6, 4)
    for (fx, fy) in ((30, 5), (30, 8), (37, 6), (33, 9), (35, 9)):
        m.set(fx, fy, m.rng.choice(("flowers_r", "flowers_y")))
    # Garden fence.
    for x in range(13, 20):
        m.set(x, 18, "fence")
        m.set(x, 22, "fence")
    for y in range(19, 22):
        m.set(13, y, "fence")
        m.set(19, y, "fence")
    m.set(16, 18, None)
    m.set(16, 18, "grass")
    for (sx, sy) in ((2, 3), (10, 3), (24, 3), (36, 16), (2, 22)):
        m.set(sx, sy, "shrub")

    m.spawn("start", 6, 8)
    m.spawn("east", 37, 12, "left")

    m.obj("warp", 39, 12, map="woods", spawn="west")
    m.obj("npc", 20, 11, npc="elder", give="sword", afterIf="got_sword",
          text="Bram: Ah, Pip! And Capo too, I see.|Capo: Morning, Elder Bram!|Bram: I'm afraid I "
               "have grim news. The Sunstone that warms the Rootwood has gone dark.|Bram: Without "
               "its light, blobs and beetles have overrun the woods east of the village.|Bram: Folk "
               "say the tiny Thimblings guard the stone, deep beneath the old stump. Far too small "
               "for us big folk...|Capo: Ahem! Too small for big folk. Not for a hero with a magic "
               "hat!|Bram: Ho ho! Then you'll need this. It's old, but it's sharp.",
          after="Bram: The Rootwood lies to the east. Find the old shrink stump, and let Capo do "
                "the rest. Be careful, Pip!")
    m.obj("npc", 37, 12, npc="guard", moveIf="got_sword", moveX="36", moveY="10",
          afterIf="got_sword",
          text="Guard: Halt, Pip! The Rootwood is crawling with monsters.|Guard: Nobody goes "
               "east without a blade. Go see Elder Bram in the square.",
          after="Guard: That's Bram's old sword! Off you go then - and watch out for beetles!")
    m.obj("npc", 25, 9, npc="mira",
          text="Mira: Morning, Pip! Did you know? Cutting bushes and smashing pots sometimes turns "
               "up gems.|Mira: Hearts too, if you're feeling bruised. Just don't tell the "
               "gardener!")
    m.obj("npc", 12, 15, npc="tobi", wander="true",
          text="Tobi: I saw a glowing stump in the woods! When I stood on it, my hat tingled.|"
               "Tobi: Mom says I'm not allowed to go back there. But you have a talking hat!")
    m.obj("sign", 35, 14, text="East: the Rootwood.\nWest: Hearthwood Village.")
    m.obj("sign", 16, 8, text="Hearthwood Village Square.\nPlease keep off the flowers.")
    # Bushes, pots and a hidden chest behind the south-east house.
    for (bx, by) in ((3, 9), (4, 9), (10, 8), (10, 9), (23, 7), (24, 7), (9, 22), (10, 22),
                     (14, 19), (15, 19), (17, 19), (18, 19), (14, 21), (18, 21), (21, 20),
                     (22, 20), (26, 21), (25, 21)):
        m.obj("bush", bx, by)
    for (px, py) in ((3, 7), (9, 7), (25, 8), (31, 13), (12, 21)):
        m.obj("pot", px, py)
    m.obj("chest", 36, 21, item="gems20")
    for (bx, by) in ((34, 20), (35, 20), (36, 20), (37, 20), (34, 21), (34, 22), (35, 22),
                     (36, 22), (37, 22), (37, 21)):
        m.obj("bush", bx, by)
    m.obj("chest", 16, 20, item="gems20")
    return m


# =============================================================================================
# The Rootwood
# =============================================================================================
def build_woods():
    m = MapBuilder("woods", 48, 32, grass_base, "The Rootwood", "woods", seed=23)
    for x in range(0, 48, 2):
        m.tree(x, 0)
        m.tree(x, 30)
    for y in range(2, 30, 2):
        if y != 12:
            m.tree(0, y)
        m.tree(46, y)
    # Inner groves.
    for (tx, ty) in ((6, 3), (10, 5), (4, 16), (6, 24), (18, 3), (22, 6), (27, 3), (30, 13),
                     (34, 12), (20, 18), (24, 22), (36, 24), (40, 16), (42, 8), (14, 26),
                     (28, 26), (32, 2), (10, 14), (42, 26)):
        m.tree(tx, ty)
    # Paths.
    m.path(0, 12, 22, 2)
    m.path(20, 9, 2, 5)
    m.path(20, 9, 18, 2)
    m.path(36, 7, 2, 4)
    m.path(22, 13, 2, 8)
    # Pond with a bridge.
    m.pond(10, 19, 9, 6)
    m.fill(10, 21, 9, 1, "bridge_h")
    m.path(19, 21, 4, 1)
    m.path(5, 21, 5, 1)
    # Rocks and shrubs.
    for (rx, ry) in ((15, 8), (16, 8), (28, 18), (29, 18), (39, 20), (8, 9), (44, 4), (25, 28)):
        m.set(rx, ry, "rock")
    for (sx, sy) in ((13, 3), (26, 10), (33, 18), (12, 11), (3, 27), (40, 29)):
        m.set(sx, sy, "shrub")
    # Fallen log on the east path.
    for i, part in enumerate(("log_l", "log_m", "log_r")):
        m.set(41 + i, 12, part)
    # The shrink stump clearing.
    m.stamp2x2("stump", 38, 4)
    for (fx, fy) in ((37, 3), (40, 3), (36, 5), (41, 6), (37, 7), (40, 7)):
        m.set(fx, fy, "flowers_w")

    m.spawn("west", 2, 12, "right")
    m.spawn("stump", 38, 7, "down")
    m.obj("warp", 0, 12, 1, 2, map="village", spawn="east")
    m.obj("stump", 38, 4, 2, 2, map="tiny", spawn="portal",
          text="Capo: This is it - the old shrink stump! I can feel it tickling my brim.|Capo: Hold "
               "on to your hat... I mean, hold on to ME!")
    m.obj("trigger", 4, 11, 1, 4, requires="got_sword",
          text="Capo: Brr. The Rootwood used to be so sunny...|Capo: Monsters ahead! Press Z to "
               "swing. Hold Z, then let go for a spin attack!")
    m.obj("sign", 35, 9, text="The old shrink stump.\nLocals say it hums at night.")
    m.obj("sign", 23, 21, text="Fishing pond. Swimming is not allowed. (Nobody can swim anyway.)")

    for (ex, ey) in ((9, 11), (15, 12), (25, 15), (31, 21), (18, 10), (40, 22)):
        m.obj("enemy", ex, ey, kind="blob", color="g")
    for (ex, ey) in ((27, 7), (36, 19)):
        m.obj("enemy", ex, ey, kind="pod")
    for (ex, ey) in ((20, 26), (40, 25), (32, 9)):
        m.obj("enemy", ex, ey, kind="beetle")

    for (bx, by) in ((5, 19), (6, 19), (7, 19), (5, 20), (30, 8), (31, 8), (32, 8), (33, 8),
                     (43, 20), (44, 20), (43, 21), (44, 21), (16, 14), (17, 14), (25, 9),
                     (26, 9), (12, 28), (13, 28), (35, 28), (36, 28), (2, 18), (3, 18),
                     (38, 9), (39, 9)):
        m.obj("bush", bx, by)
    m.obj("chest", 3, 21, item="gems20")
    return m


# =============================================================================================
# Thimblewick (the tiny world)
# =============================================================================================
def build_tiny():
    m = MapBuilder("tiny", 40, 26, soil_base, "Thimblewick", "tiny", seed=37)
    for x in range(0, 40):
        m.set(x, 0, "root_wall")
        m.set(x, 1, "root_wall_b")
        m.blade(x, 24, "blade" if x % 3 else "blade2")
    for y in range(2, 24, 2):
        m.blade(0, y, "blade2")
        m.blade(39, y, "blade")
    m.stamp2x2("hollow", 19, 0)
    # Blade walls that make a winding path north.
    for x in range(1, 31):
        if x not in (26, 27):
            m.blade(x, 13, "blade" if x % 2 else "blade2")
    for x in range(10, 39):
        if x not in (13, 14):
            m.blade(x, 6, "blade2" if x % 2 else "blade")
    # Obstacles.
    m.stamp2x2("pebble", 7, 7)
    m.stamp2x2("acorn", 30, 3)
    m.stamp2x2("acorn", 16, 17)
    m.stamp2x2("pebble", 32, 18)
    m.stamp2x2("pebble", 24, 9)
    m.pond(20, 18, 6, 4)

    m.spawn("portal", 6, 21, "right")
    m.spawn("hollow", 19.5, 3, "down")
    m.obj("portal", 2.5, 20.5, 2, 1.25, map="woods", spawn="stump")
    m.obj("warp", 19, 1, 2, 1, map="dungeon", spawn="entrance")
    m.obj("npc", 10, 19, npc="thim",
          text="Thimbling: Oh my! A big-folk, made small! Welcome to Thimblewick.|Thimbling: The "
               "beetles have taken Root Hollow, and their master swallowed the Sunstone's light!|"
               "Thimbling: The hollow is to the north. Mind the ants - they bite!")
    m.obj("npc", 6, 16, npc="thim2",
          text="Thimbling: When you want to be big again, stand on the leaf portal and ask your "
               "hat.|Capo: Ask nicely, please.")
    m.obj("sign", 24, 16, text="Dew Pond.\nFresh this morning!")
    m.obj("trigger", 18, 10, 4, 2,
          text="Capo: Look at the size of that grass! Or... the size of us.|Capo: That dark "
               "hollow up north must be Root Hollow. Let's go!")
    for (ex, ey) in ((14, 16), (28, 20), (33, 10), (22, 4), (5, 9)):
        m.obj("enemy", ex, ey, kind="ant")
    for (ex, ey) in ((11, 9), (34, 15)):
        m.obj("enemy", ex, ey, kind="blob", color="b")
    for (bx, by) in ((3, 4), (4, 4), (36, 9), (36, 10), (12, 21), (29, 22)):
        m.obj("pot", bx, by)
    return m


# =============================================================================================
# Root Hollow (dungeon): 3x3 rooms of 15x10 tiles
# =============================================================================================
RW, RH = 15, 10


def build_dungeon():
    m = MapBuilder("dungeon", RW * 3, RH * 3, lambda x, y, r: "dwall_c", "Root Hollow", "dungeon",
                   rooms=True, seed=51)
    active = [(1, 2), (0, 1), (1, 1), (2, 1), (0, 0), (1, 0), (2, 0)]
    for (c, r) in active:
        ox, oy = c * RW, r * RH
        for y in range(RH):
            for x in range(RW):
                if x in (0, RW - 1) and y in (0, RH - 1):
                    name = "dwall_c"
                elif y == 0:
                    name = "dwall_t"
                elif y == RH - 1:
                    name = "dwall_b"
                elif x == 0:
                    name = "dwall_l"
                elif x == RW - 1:
                    name = "dwall_r"
                else:
                    rr = m.rng.random()
                    name = "dfloor_roots" if rr < 0.06 else ("dfloor2" if rr < 0.4 else "dfloor")
                m.set(ox + x, oy + y, name)

    def link_ns(c, r_lower, kind=None, **props):
        x = c * RW + 7
        m.set(x, r_lower * RH, "dfloor")
        m.set(x, r_lower * RH - 1, "dfloor")
        if kind:
            m.obj("door", x, r_lower * RH - 1, 1, 2, kind=kind, **props)

    def link_ew(c_left, r, kind=None, **props):
        y = r * RH + 4
        m.set(c_left * RW + RW - 1, y, "dfloor")
        m.set((c_left + 1) * RW, y, "dfloor")
        if kind:
            m.obj("door", c_left * RW + RW - 1, y, 2, 1, kind=kind, **props)

    def at(c, r, x, y):
        return c * RW + x, r * RH + y

    # Connections.
    link_ns(1, 2, "shutter")          # entrance <-> hub
    link_ew(0, 1, "shutter")          # west <-> hub
    link_ew(1, 1, "locked")           # hub <-> east
    link_ns(1, 1, "boss")             # hub <-> boss
    link_ns(1, 1, "shutter")          # boss room locks while the boss lives
    link_ns(0, 1)                     # west <-> north-west
    link_ns(2, 1, "shutter")          # east <-> north-east
    # Exit at the bottom of the entrance room.
    ex, ey = at(1, 2, 7, 9)
    m.set(ex, ey, "dfloor")
    m.obj("warp", ex, ey, map="tiny", spawn="hollow")

    # -- entrance (1,2)
    for y in range(1, 9):
        m.set(*at(1, 2, 7, y), "carpet")
    m.spawn("entrance", *at(1, 2, 7, 7), direction="up")
    m.obj("torch", *at(1, 2, 3, 2))
    m.obj("torch", *at(1, 2, 11, 2))
    m.obj("pot", *at(1, 2, 1, 8), drop="heart")
    m.obj("pot", *at(1, 2, 13, 8), drop="gem_b")
    m.obj("pot", *at(1, 2, 13, 1))
    m.obj("sign", *at(1, 2, 5, 5),
          text="ROOT HOLLOW\nThe Bramble Beetle sleeps within.\nTurn back, small one!")
    m.obj("trigger", *at(1, 2, 6, 4), 3, 1,
          text="Capo: Brr, it's damp in here.|Capo: Doors that won't budge usually need a key... or "
               "a clever switch. Let's look around!")

    # -- hub (1,1)
    for (x, y) in ((2, 2), (12, 2), (2, 7), (12, 7)):
        m.set(*at(1, 1, x, y), "dstatue")
    for (x, y) in ((4, 3), (10, 3), (4, 6), (10, 6)):
        m.obj("enemy", *at(1, 1, x, y), kind="blob", color="r")

    # -- west (0,1): push the block onto the heavy switch
    m.set(*at(0, 1, 2, 2), "dstatue")
    m.set(*at(0, 1, 2, 3), "dstatue")
    m.set(*at(0, 1, 1, 3), "dstatue")
    m.obj("switch", *at(0, 1, 3, 2), heavy="true", target="w_switch")
    m.obj("block", *at(0, 1, 6, 5), lock="w_switch")
    m.obj("chest", *at(0, 1, 10, 7), item="key", showIf="w_switch")
    m.obj("sign", *at(0, 1, 9, 4),
          text="A heavy switch. Something bigger than you will have to sit on it.")
    m.obj("enemy", *at(0, 1, 12, 1), kind="pod")
    m.obj("enemy", *at(0, 1, 2, 7), kind="pod")

    # -- north-west (0,0): optional treasure room
    m.obj("pot", *at(0, 0, 1, 1), drop="heart")
    m.obj("pot", *at(0, 0, 13, 1), drop="gem_b")
    m.obj("pot", *at(0, 0, 1, 8), drop="heart")
    m.obj("pot", *at(0, 0, 13, 8), drop="gem_r")
    m.obj("chest", *at(0, 0, 7, 3), item="gems50")
    for (x, y) in ((5, 3), (9, 3), (5, 6), (9, 6)):
        m.set(*at(0, 0, x, y), "dstatue")
    m.obj("enemy", *at(0, 0, 4, 5), kind="bat")
    m.obj("enemy", *at(0, 0, 10, 4), kind="bat")

    # -- east (2,1): fight room
    for (x, y) in ((4, 3), (10, 6)):
        m.set(*at(2, 1, x, y), "dstatue")
    m.obj("enemy", *at(2, 1, 3, 6), kind="beetle")
    m.obj("enemy", *at(2, 1, 11, 3), kind="beetle")
    m.obj("enemy", *at(2, 1, 7, 5), kind="blob", color="r")
    m.obj("enemy", *at(2, 1, 12, 7), kind="pod")
    m.obj("enemy", *at(2, 1, 7, 2), kind="bat")

    # -- north-east (2,0): two blocks onto two switches
    for sx, flag in ((4, "ne_s1"), (10, "ne_s2")):
        m.set(*at(2, 0, sx, 2), "dstatue")
        m.obj("switch", *at(2, 0, sx, 3), heavy="true", target=flag)
        m.obj("block", *at(2, 0, sx, 6), lock=flag)
    m.obj("chest", *at(2, 0, 7, 2), item="bigkey", showIf="ne_s1&ne_s2")
    m.obj("enemy", *at(2, 0, 7, 6), kind="bat")
    m.obj("sign", *at(2, 0, 1, 7), text="Two heavy switches. Two heavy blocks. Coincidence?")

    # -- boss (1,0)
    m.obj("torch", *at(1, 0, 1, 1))
    m.obj("torch", *at(1, 0, 13, 1))
    m.obj("enemy", *at(1, 0, 7, 4), kind="boss")
    m.obj("pickup", *at(1, 0, 7, 2), item="sunstone", showIf="boss_dead")
    m.obj("trigger", *at(1, 0, 6, 7), 3, 1, requires="!boss_dead",
          text="Capo: That's the Bramble Beetle! Its shell is too tough for your sword...|Capo: "
               "Make it charge into a wall. When it's flipped over, strike its belly!")
    return m


def write_tsj(out_dir, tileset):
    tiles = []
    for key, props in sorted(tileset["properties"].items(), key=lambda kv: int(kv[0])):
        p = []
        for k, v in props.items():
            t = "bool" if isinstance(v, bool) else "int"
            p.append({"name": k, "type": t, "value": v})
        tiles.append({"id": int(key), "properties": p})
    doc = {"type": "tileset", "version": "1.10", "name": "tiles", "tilewidth": T, "tileheight": T,
           "columns": tileset["columns"], "tilecount": tileset["tilecount"],
           "image": "tiles.png", "imagewidth": tileset["imagewidth"],
           "imageheight": tileset["imageheight"], "margin": 0, "spacing": 0, "tiles": tiles}
    with open(os.path.join(out_dir, "tiles.tsj"), "w") as f:
        json.dump(doc, f, indent=1)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "resources/maps"
    os.makedirs(out_dir, exist_ok=True)
    tex_dir = os.path.join(os.path.dirname(os.path.abspath(out_dir)), "textures")
    with open(os.path.join(tex_dir, "tileset.json")) as f:
        tileset = json.load(f)
    write_tsj(tex_dir, tileset)
    for builder in (build_village, build_woods, build_tiny, build_dungeon):
        builder().export(out_dir, tileset)


if __name__ == "__main__":
    main()
