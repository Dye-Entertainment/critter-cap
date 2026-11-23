#!/usr/bin/env python3
"""Builds Critter Cap's maps and writes them as Tiled JSON (.json maps + tiles.tsj tileset).

Usage: make_maps.py <out_dir>   (reads ../textures/tileset.json next to out_dir)
The maps open in the Tiled editor; the game reads the same files.
"""
import json
import os
import random
import sys

T = 16


class MapBuilder:
    def __init__(self, name, w, h, base, title, music, rooms=False, seed=1, encounters="",
                 wild="grass", heal=""):
        self.name, self.w, self.h = name, w, h
        self.encounters, self.wild, self.heal = encounters, wild, heal
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

    def grass(self, x, y, w, h, kind="tallgrass"):
        """Tall grass where wild critters hide; only fills tiles that are still plain ground."""
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if self.free(xx, yy) and not self.dirt[yy][xx]:
                    self.tiles[yy][xx] = kind if (xx + yy) % 3 else kind + "2"

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
                {"name": "encounters", "type": "string", "value": self.encounters},
                {"name": "wild", "type": "string", "value": self.wild},
                {"name": "heal", "type": "string", "value": self.heal},
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
    m = MapBuilder("village", 40, 26, grass_base, "Hearthwood", "village", seed=11,
                   encounters="blob_g:2-4:35,ant:2-4:40,pod:3-4:25", heal="mira")
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
    # A little patch of tall grass behind the garden, to practise catching.
    m.grass(21, 19, 6, 4)

    m.spawn("start", 6, 8)
    m.spawn("east", 37, 12, "left")
    m.spawn("mira", 25, 11, "up")

    m.obj("warp", 39, 12, map="woods", spawn="west")
    m.obj("npc", 20, 11, npc="elder", starter="true", give="caps5", afterIf="got_starter",
          text="Bram: Ah, Pip! And Capo too, I see.|Capo: Morning, Elder Bram! Is it time? Is it "
               "time?|Bram: Ho ho! It is. Every young one in Hearthwood gets a critter partner.|"
               "Bram: But the Sunstone deep in Root Hollow has gone dark, and the wild critters are "
               "restless. You'll need a brave partner.|Bram: Three little blobs are waiting. Pick "
               "the one you like best!",
          after="Bram: Wild critters hide in tall grass. Wear one down in battle, then throw a "
                "cap!|Bram: Catch them all, and maybe you can calm the master of Root Hollow "
                "itself.")
    m.obj("npc", 37, 12, npc="guard", moveIf="got_starter", moveX="36", moveY="10",
          afterIf="got_starter",
          text="Guard: Hold on, Pip! The Rootwood's tall grass is full of wild critters.|Guard: "
               "Nobody goes east without a partner. Go see Elder Bram in the square.",
          after="Guard: A partner! Good. Walk through tall grass to find wild critters, and hold "
                "X to run.")
    m.obj("npc", 25, 9, npc="mira", heal="true", shop="true", healSpawn="mira",
          text="Mira: Hi, Pip! I look after tired critters. Let me see your team...")
    m.obj("npc", 30, 12, npc="tobi", dir="left", sight="5", team="rival:5", trainer="Tobi",
          prize=20, winFlag="tobi1", afterIf="tobi1",
          text="Tobi: Pip! Grandpa Bram gave you a critter too?|Tobi: I picked mine because it "
               "beats yours. Let's battle!",
          win="Tobi: Aww, no fair! I'm going to train in the Rootwood. Next time I'll win!",
          after="Tobi: You got lucky! I'm off to the Rootwood to train.")
    m.obj("npc", 12, 15, npc="thim2", wander="true",
          text="Thimbling: Hello, big folk! I came up from Thimblewick to see the village.|"
               "Thimbling: Down there everything is tiny... even the critters!")
    m.obj("sign", 35, 14, text="East: the Rootwood.\nWest: Hearthwood Village.")
    m.obj("sign", 16, 8, text="Hearthwood Village Square.\nCritters welcome. Please keep off the "
                              "flowers.")
    m.obj("sign", 20, 18, text="Tall grass! Wild critters hide here. Press Enter to check your "
                               "team.")
    # Bushes, pots and a hidden chest behind the south-east house.
    for (bx, by) in ((3, 9), (4, 9), (10, 8), (10, 9), (23, 7), (24, 7), (9, 22), (10, 22),
                     (14, 19), (15, 19), (17, 19), (18, 19), (14, 21), (18, 21)):
        m.obj("bush", bx, by)
    for (px, py) in ((3, 7), (9, 7), (31, 13), (12, 21)):
        m.obj("pot", px, py)
    m.obj("chest", 36, 21, item="gems20")
    for (bx, by) in ((34, 20), (35, 20), (36, 20), (37, 20), (34, 21), (34, 22), (35, 22),
                     (36, 22), (37, 22), (37, 21)):
        m.obj("bush", bx, by)
    m.obj("chest", 16, 20, item="caps5")
    return m


# =============================================================================================
# The Rootwood
# =============================================================================================
def build_woods():
    m = MapBuilder("woods", 48, 32, grass_base, "The Rootwood", "woods", seed=23,
                   encounters="blob_g:4-6:25,pod:4-7:25,beetle:5-7:15,bat:5-6:15,ant:4-6:20")
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
    # Tall grass meadows on both sides of the trail.
    m.grass(2, 14, 8, 2)
    m.grass(6, 6, 9, 5)
    m.grass(12, 14, 8, 4)
    m.grass(24, 12, 6, 6)
    m.grass(25, 19, 10, 3)
    m.grass(30, 22, 12, 6)
    m.grass(16, 26, 8, 3)
    m.grass(38, 13, 7, 3)
    m.grass(2, 22, 6, 6)

    m.spawn("west", 2, 12, "right")
    m.spawn("stump", 38, 7, "down")
    m.obj("warp", 0, 12, 1, 2, map="village", spawn="east")
    m.obj("stump", 38, 4, 2, 2, map="tiny", spawn="portal", requires="ranger",
          locked="Capo: The stump is humming, but its magic won't let us in...|Capo: The ranger "
                 "guards it. Maybe we have to prove ourselves first!",
          text="Capo: This is it - the old shrink stump! I can feel it tickling my brim.|Capo: Hold "
               "on to your hat... I mean, hold on to ME!")
    m.obj("npc", 36, 10, npc="guard", dir="down", sight="3", team="beetle:7,pod:7",
          trainer="Ranger Rook", prize=30, winFlag="ranger", afterIf="ranger",
          text="Rook: Halt! I'm Ranger Rook, keeper of the shrink stump.|Rook: Only trainers who "
               "can beat me get to go small. Ready?",
          win="Rook: Well battled! The stump is yours to use. Thimblewick is waiting below.",
          after="Rook: Step onto the stump and let your hat do the rest. Good luck down there!")
    m.obj("npc", 22, 18, npc="tobi", dir="up", sight="4", team="ant:6,rival:7", trainer="Tobi",
          prize=25, winFlag="tobi2", afterIf="tobi2", hideIf="!tobi1",
          text="Tobi: Pip! I've been training all morning. And I caught a new critter!|Tobi: "
               "This time I'll win for sure!",
          win="Tobi: Argh! How are you so good at this?",
          after="Tobi: I'm going home to rest my team. Grandpa Bram will be so proud of you...")
    m.obj("trigger", 4, 11, 1, 4, requires="got_starter",
          text="Capo: The Rootwood! Wild critters love this tall grass.|Capo: Battle them to "
               "wear them down. When they're weak, choose CAP and I'll do the rest!")
    m.obj("sign", 35, 9, text="The old shrink stump.\nLocals say it hums at night.")
    m.obj("sign", 23, 21, text="Fishing pond. Drizzlets have been seen hopping in the shallows.")
    m.obj("sign", 5, 11, text="Hold X to run. Wild critters can't catch you on the trail!")

    for (bx, by) in ((5, 19), (6, 19), (7, 19), (5, 20), (30, 8), (31, 8), (32, 8), (33, 8),
                     (43, 20), (44, 20), (43, 21), (44, 21), (16, 18), (17, 18), (25, 9),
                     (26, 9), (12, 28), (13, 28), (35, 28), (36, 28), (2, 18), (3, 18),
                     (38, 9), (39, 9)):
        m.obj("bush", bx, by)
    m.obj("chest", 3, 21, item="caps5")
    m.obj("chest", 44, 22, item="gems20")
    return m


# =============================================================================================
# Thimblewick (the tiny world)
# =============================================================================================
def build_tiny():
    m = MapBuilder("tiny", 40, 26, soil_base, "Thimblewick", "tiny", seed=37,
                   encounters="ant:7-9:30,blob_b:7-9:25,beetle:8-10:20,bat:7-9:15,pod:8-9:10",
                   heal="thim")
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
    # Moss beds, the tiny world's tall grass.
    m.grass(27, 15, 11, 7, "moss")
    m.grass(1, 8, 6, 4, "moss")
    m.grass(14, 8, 9, 4, "moss")
    m.grass(28, 8, 10, 4, "moss")
    m.grass(1, 2, 8, 3, "moss")
    m.grass(24, 2, 5, 3, "moss")

    m.spawn("portal", 6, 21, "right")
    m.spawn("hollow", 19.5, 3, "down")
    m.spawn("thim", 10, 21, "up")
    m.obj("portal", 2.5, 20.5, 2, 1.25, map="woods", spawn="stump")
    m.obj("warp", 19, 1, 2, 1, map="dungeon", spawn="entrance")
    m.obj("npc", 10, 19, npc="thim", heal="true", shop="true", healSpawn="thim",
          text="Thimbling: Oh my! A big-folk, made small! Welcome to Thimblewick.|Thimbling: Your "
               "critters look tired. Let me fix them right up...")
    m.obj("npc", 22, 15, npc="thim2", dir="down", sight="3", team="blob_b:9,bat:9,ant:10",
          trainer="Pipkin", prize=40, winFlag="pipkin", afterIf="pipkin",
          text="Pipkin: Halt, giant! I'm the best trainer in all of Thimblewick!|Pipkin: Small "
               "critters, big hearts. Let's go!",
          win="Pipkin: You're... you're really good! The master of Root Hollow is up north. It "
              "swallowed the Sunstone's light!",
          after="Pipkin: Bramblord is huge, but caps work on big critters too. Bring plenty!")
    m.obj("sign", 24, 16, text="Dew Pond.\nFresh this morning!")
    m.obj("sign", 18, 4, text="ROOT HOLLOW\nHome of Bramblord, master of the critters.")
    m.obj("trigger", 18, 10, 4, 2,
          text="Capo: Look at the size of that grass! Or... the size of us.|Capo: That dark "
               "hollow up north must be Root Hollow. Bramblord lives there!")
    for (bx, by) in ((3, 4), (4, 4), (36, 9), (36, 10), (12, 21), (29, 22)):
        m.obj("pot", bx, by)
    return m


# =============================================================================================
# Root Hollow (dungeon): 3x3 rooms of 15x10 tiles
# =============================================================================================
RW, RH = 15, 10


def build_dungeon():
    m = MapBuilder("dungeon", RW * 3, RH * 3, lambda x, y, r: "dwall_c", "Root Hollow", "dungeon",
                   rooms=True, seed=51, wild="everywhere",
                   encounters="bat:9-11:35,beetle:10-12:25,blob_r:9-11:25,ant:10-12:15")
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
    link_ns(1, 2)                     # entrance <-> hub
    link_ew(0, 1)                     # west <-> hub
    link_ew(1, 1, "locked")           # hub <-> east
    link_ns(1, 1, "boss")             # hub <-> master's chamber
    link_ns(0, 1)                     # west <-> north-west
    link_ns(2, 1)                     # east <-> north-east
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
    m.obj("pot", *at(1, 2, 1, 8), drop="cap")
    m.obj("pot", *at(1, 2, 13, 8), drop="gem_b")
    m.obj("pot", *at(1, 2, 13, 1), drop="cap")
    m.obj("sign", *at(1, 2, 5, 5),
          text="ROOT HOLLOW\nBramblord sleeps within.\nTurn back, small one!")
    m.obj("trigger", *at(1, 2, 6, 4), 3, 1,
          text="Capo: Brr, it's damp in here. Critters can jump out anywhere in a cave!|Capo: "
               "Doors that won't budge usually need a key... or a clever switch.")

    # -- hub (1,1)
    for (x, y) in ((2, 2), (12, 2), (2, 7), (12, 7)):
        m.set(*at(1, 1, x, y), "dstatue")
    m.obj("pot", *at(1, 1, 4, 3), drop="cap")
    m.obj("pot", *at(1, 1, 10, 6))

    # -- west (0,1): push the block onto the heavy switch
    m.set(*at(0, 1, 2, 2), "dstatue")
    m.set(*at(0, 1, 2, 3), "dstatue")
    m.set(*at(0, 1, 1, 3), "dstatue")
    m.obj("switch", *at(0, 1, 3, 2), heavy="true", target="w_switch")
    m.obj("block", *at(0, 1, 6, 5), lock="w_switch")
    m.obj("chest", *at(0, 1, 10, 7), item="key", showIf="w_switch")
    m.obj("sign", *at(0, 1, 9, 4),
          text="A heavy switch. Something bigger than you will have to sit on it.")

    # -- north-west (0,0): optional treasure room
    m.obj("pot", *at(0, 0, 1, 1), drop="cap")
    m.obj("pot", *at(0, 0, 13, 1), drop="gem_b")
    m.obj("pot", *at(0, 0, 1, 8), drop="cap")
    m.obj("pot", *at(0, 0, 13, 8), drop="gem_r")
    m.obj("chest", *at(0, 0, 7, 3), item="caps10")
    for (x, y) in ((5, 3), (9, 3), (5, 6), (9, 6)):
        m.set(*at(0, 0, x, y), "dstatue")

    # -- east (2,1)
    for (x, y) in ((4, 3), (10, 6)):
        m.set(*at(2, 1, x, y), "dstatue")
    m.obj("chest", *at(2, 1, 12, 7), item="gems50")

    # -- north-east (2,0): two blocks onto two switches
    for sx, flag in ((4, "ne_s1"), (10, "ne_s2")):
        m.set(*at(2, 0, sx, 2), "dstatue")
        m.obj("switch", *at(2, 0, sx, 3), heavy="true", target=flag)
        m.obj("block", *at(2, 0, sx, 6), lock=flag)
    m.obj("chest", *at(2, 0, 7, 2), item="bigkey", showIf="ne_s1&ne_s2")
    m.obj("sign", *at(2, 0, 1, 7), text="Two heavy switches. Two heavy blocks. Coincidence?")

    # -- master's chamber (1,0)
    m.obj("torch", *at(1, 0, 1, 1))
    m.obj("torch", *at(1, 0, 13, 1))
    m.obj("legend", *at(1, 0, 7, 4), critter="boss:15", caughtFlag="legend_caught",
          text="Capo: That's Bramblord, master of Root Hollow! It's waking up...|Capo: It won't "
               "run away, and neither will we. Wear it down, then throw caps!")
    m.obj("pickup", *at(1, 0, 7, 2), item="sunstone", showIf="legend_caught")
    m.obj("trigger", *at(1, 0, 6, 7), 3, 1, requires="!legend_caught",
          text="Capo: Shh... it's sleeping. That's the biggest critter I've ever seen!|Capo: Check "
               "your team with Enter before you wake it. Mira's caps are our best friend now.")
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
