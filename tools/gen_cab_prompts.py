# SPDX-License-Identifier: MIT
# Copyright (c) 2026 singularity098
"""Writes the image prompts for the IR blocks' cabinet images, one per line (1:1 images).

python3 tools/gen_cab_prompts.py docs/image-prompts/ir-cabinets.txt
200 prompts: 75% guitar (1x12, 2x12, 4x12), 25% bass (1x10, 2x10, 4x10; some with aluminum cones),
grills of cloth, tweed, metal or none, across genres. Seeded, so the output is the same every run.
"""
import random, sys
rng = random.Random(2026)

GENRES = {
 "heavy metal": dict(
   cover=["black textured tolex", "black snakeskin vinyl", "matte black bed-liner coating", "black tolex with scuffed edges", "charred black stained wood"],
   trim=["chrome corner protectors", "heavy steel corner caps", "black metal corners", "riveted steel edging"],
   scene=["on a fog-filled stage", "in front of a wall of stacked cabinets fading into darkness", "on a riser with smoke rolling across the floor", "in a dark rehearsal bunker with concrete walls"],
   light=["harsh red backlight cutting through haze", "cold blue rim light and red spotlights", "lightning-white strobe light frozen mid-flash", "a single hard top light with deep shadows"],
   mood=["menacing and massive", "brutal and imposing", "aggressive, high-contrast"]),
 "doom metal": dict(
   cover=["worn black tolex", "weathered dark oak", "black crocodile-grain vinyl"],
   trim=["tarnished brass corners", "blackened iron corner caps"],
   scene=["in a cavernous stone cellar", "surrounded by heavy purple smoke"],
   light=["dim violet haze with a low amber glow", "murky green and purple stage light"],
   mood=["slow, heavy and ominous", "occult, smoky atmosphere"]),
 "funk": dict(
   cover=["glossy white tolex", "orange sparkle vinyl", "cream tolex", "purple glitter vinyl", "gold metal-flake vinyl"],
   trim=["polished chrome corners", "gold corner protectors", "chrome piping"],
   scene=["on a disco dance floor with lit-up tiles", "on a 1970s soundstage with a mirror ball overhead", "in a groovy lounge with shag carpet", "in front of a sunburst stage backdrop"],
   light=["warm magenta and orange party lights", "sparkling mirror-ball reflections", "saturated purple and gold lighting", "colorful gel spotlights in pink and teal"],
   mood=["groovy, joyful and bright", "retro 1970s feel", "playful and soulful"]),
 "classic rock": dict(
   cover=["black levant tolex", "worn brown tolex", "oxblood vinyl", "vintage cream tolex", "red tolex"],
   trim=["chrome corners", "brass corner protectors", "white piping", "gold piping"],
   scene=["on a wooden stage at a packed arena", "in a vintage recording studio with wood paneling", "on a well-worn stage with a Persian rug", "backstage next to road cases"],
   light=["warm amber stage lighting", "golden spotlight with dust in the air", "classic tungsten studio lighting", "warm sunset light through a stage door"],
   mood=["timeless and confident", "warm, nostalgic 1970s feel", "road-worn and loved"]),
 "punk": dict(
   cover=["beat-up black tolex covered in stickers with no readable text", "spray-painted tolex with abstract splatters", "duct-taped black tolex", "torn red vinyl patched with tape", "safety-pinned plaid fabric"],
   trim=["dented metal corners", "mismatched corner caps", "no corner protectors, chipped edges"],
   scene=["in a grimy basement venue with graffiti walls", "on a sticky club floor covered in flyers with no readable text", "in a cramped garage with a bare bulb", "on the floor of a DIY squat show"],
   light=["harsh on-camera flash", "flickering fluorescent light", "a single bare bulb with green and pink gel spill", "gritty mixed lighting"],
   mood=["raw, loud and rebellious", "chaotic DIY energy", "gritty xerox-zine feel"]),
 "pop": dict(
   cover=["pastel pink tolex", "mint green tolex", "glossy white lacquer", "baby blue tolex", "lavender vinyl", "iridescent pearl vinyl"],
   trim=["rose-gold corners", "white corners", "polished chrome corners"],
   scene=["on a clean seamless studio backdrop", "on a bright pastel stage set", "in a colorful photo studio with geometric props", "on a glossy floor with balloon-shaped props"],
   light=["soft even beauty lighting", "bright candy-colored gradient lighting", "soft pink and cyan glow", "bright high-key lighting"],
   mood=["clean, cheerful and polished", "playful modern pop aesthetic", "glossy and fresh"]),
 "blues": dict(
   cover=["lacquered tweed", "brown tolex", "worn tan tolex", "dark walnut wood"],
   trim=["brass corners", "leather corner caps", "no corners, rounded edges"],
   scene=["in a smoky juke joint", "on a small bar stage with blank neon signs glowing on the wall", "on a creaky porch at dusk", "in a dim club with brick walls"],
   light=["warm low tungsten light and cigarette-smoke haze", "deep blue light with a warm amber key", "dusky golden light"],
   mood=["soulful and lived-in", "intimate late-night mood", "weathered and warm"]),
 "jazz": dict(
   cover=["dark brown tolex", "black leatherette", "satin walnut veneer", "charcoal tolex"],
   trim=["small chrome corners", "brass piping", "no corners, clean edges"],
   scene=["in an intimate jazz club with small round tables", "beside an upright piano in a lounge", "on a polished wooden stage"],
   light=["moody blue and warm amber light", "soft spotlight with bokeh in the background", "warm low-key lighting"],
   mood=["sophisticated and smooth", "elegant late-night feel"]),
 "country": dict(
   cover=["tan tolex", "blonde tolex", "rustic barnwood", "tooled leather-look vinyl"],
   trim=["brass corners", "leather handles and corners", "copper corner caps"],
   scene=["in a rustic barn with hay bales", "on a honky-tonk stage", "on a wooden porch at golden hour"],
   light=["golden hour sunlight", "warm string lights", "dusty warm sunbeams"],
   mood=["rustic, warm and honest", "Americana feel"]),
 "surf": dict(
   cover=["seafoam green tolex", "coral red tolex", "white tolex", "turquoise sparkle vinyl"],
   trim=["chrome corners", "white piping", "chrome trim strips"],
   scene=["on a beach boardwalk", "in a 1960s beach party set with palm trees", "on sand at a tiki bar"],
   light=["bright sunny daylight", "sunset orange and pink sky light", "tiki torch glow at dusk"],
   mood=["sunny and retro 1960s", "breezy and carefree"]),
 "grunge": dict(
   cover=["faded, peeling black tolex", "worn flannel-pattern fabric", "scratched gray tolex", "dirty olive green tolex"],
   trim=["rusty corners", "scuffed black corners"],
   scene=["in a rainy-day garage", "in a cluttered rehearsal room with old rugs", "in a warehouse with wet concrete floor"],
   light=["overcast gray window light", "dim muted green-tinted light", "desaturated cold light"],
   mood=["moody, muted and raw", "early 1990s feel"]),
 "shoegaze": dict(
   cover=["soft gray tolex", "faded pink tolex", "cream tolex"],
   trim=["chrome corners", "white corners"],
   scene=["surrounded by soft drifting haze", "in a dreamy room with sheer curtains"],
   light=["dreamy pink and blue washed light with heavy bloom", "soft hazy pastel light with lens flare"],
   mood=["dreamy, hazy and ethereal", "soft-focus wall of sound feel"]),
 "synthwave pop": dict(
   cover=["black gloss tolex", "magenta vinyl", "chrome-finish vinyl", "deep purple tolex"],
   trim=["neon-lit edge strips", "chrome corners"],
   scene=["on a grid floor with a retro sunset horizon", "in a neon-lit city alley at night", "on a stage with laser beams"],
   light=["hot pink and cyan neon lighting", "purple and teal laser light", "neon glow reflecting on wet ground"],
   mood=["retro-futuristic 1980s", "bold neon aesthetic"]),
 "reggae": dict(
   cover=["dark green tolex", "black tolex", "warm natural wood"],
   trim=["brass corners", "black corners"],
   scene=["on an outdoor sound system stage", "in a sunny yard with tropical plants"],
   light=["warm tropical sunlight", "red, gold and green stage lights"],
   mood=["relaxed and warm", "laid-back and sunny"]),
 "stoner rock": dict(
   cover=["orange tolex", "mustard yellow tolex", "brown suede-look vinyl"],
   trim=["brass corners", "black corners"],
   scene=["in the desert at dusk", "in a wood-paneled 1970s den", "on a stage in the desert sand"],
   light=["dusty orange desert sunset", "warm amber haze", "psychedelic orange and purple light"],
   mood=["fuzzy, warm and psychedelic", "desert rock feel"]),
 "hardcore": dict(
   cover=["black tolex", "scuffed gray tolex", "black carpet covering"],
   trim=["steel corners", "dented black corners"],
   scene=["in a packed VFW hall", "on a low stage with no barrier", "in a concrete-floored venue"],
   light=["harsh white stage light", "red and white strobing light", "stark on-camera flash"],
   mood=["intense and raw", "high-energy and stark"]),
 "rockabilly": dict(
   cover=["two-tone cream and red tolex", "black and white tolex", "turquoise tolex", "sparkle red vinyl"],
   trim=["chrome corners", "white piping", "chrome trim"],
   scene=["in a 1950s diner", "at a vintage car show", "on a checkerboard dance floor"],
   light=["bright chrome-reflecting diner light", "warm vintage stage light", "pink and teal neon glow"],
   mood=["retro 1950s and fun", "vintage hot-rod feel"]),
 "progressive rock": dict(
   cover=["deep blue tolex", "silver tolex", "dark purple tolex", "black tolex"],
   trim=["chrome corners", "brushed aluminum trim"],
   scene=["on a grand stage with a cosmic projection behind", "in a futuristic studio", "in front of a starfield backdrop"],
   light=["cosmic blue and violet light beams", "colorful moving lights", "dramatic spotlight beams through haze"],
   mood=["epic and cinematic", "otherworldly"]),
 "indie rock": dict(
   cover=["mustard tolex", "teal tolex", "brown tolex", "faded red tolex", "olive green tolex"],
   trim=["chrome corners", "black corners"],
   scene=["in a cozy living room with houseplants", "on a small club stage with fairy lights", "in a sunlit loft"],
   light=["soft natural window light", "warm fairy lights", "golden afternoon light"],
   mood=["cozy, charming and understated", "lo-fi film photo feel"]),
}
GUITAR_GENRES = ["heavy metal","doom metal","funk","classic rock","punk","pop","blues","jazz","country","surf","grunge","shoegaze","synthwave pop","stoner rock","hardcore","rockabilly","progressive rock","indie rock"]
BASS_GENRES = ["funk","heavy metal","classic rock","punk","pop","jazz","reggae","doom metal","synthwave pop","grunge","hardcore","progressive rock","indie rock","stoner rock"]

CLOTH = ["salt-and-pepper grill cloth", "oxblood grill cloth", "wheat-colored grill cloth", "black basketweave grill cloth",
 "silver sparkle grill cloth", "brown-and-gold striped grill cloth", "dark gray grill cloth", "cream grill cloth",
 "deep red grill cloth", "blue-and-silver grill cloth", "black grill cloth with a faint silver thread", "tan woven grill cloth",
 "maroon basketweave grill cloth", "green woven grill cloth", "plain black acoustic cloth"]
TWEED = ["lacquered tweed covering with a brown linen grill cloth", "tweed covering with an oxblood grill cloth",
 "aged tweed covering with a wheat grill cloth", "tweed covering with a brown-and-gold striped grill cloth"]
METAL = ["a perforated steel grill", "an expanded-metal diamond grate", "a black powder-coated steel mesh grill",
 "a chrome wire grill", "a heavy hexagonal metal grate", "a punched aluminum grill with round holes",
 "a brushed silver metal grill", "a black square-mesh steel grate"]
OPEN_ = ["no grill, speakers fully exposed", "an open front with the speakers exposed", "no grill cloth, the speaker cones bare"]

GUITAR_CONES = ["dark gray paper cones with domed dust caps", "black paper cones with felt dust caps", "tan paper cones with ribbed surrounds",
  "cream paper cones", "dark paper cones with chrome dust caps", "black paper cones with bronze dust caps"]
BASS_PAPER = ["black paper cones with large dust caps", "dark gray paper cones with rubber surrounds", "black treated cones"]
BASS_ALU = ["spun aluminum cones with a brushed metallic shine", "polished aluminum cones with concentric spun rings",
 "brushed aluminum cones catching the light", "silver aluminum cones with chrome dust caps"]

LAYOUT = {
 "1x12": ["a compact 1x12 guitar speaker cabinet with one 12-inch speaker", "a small 1x12 guitar speaker cab with a single 12-inch speaker", "a 1x12 open-back guitar cabinet with one 12-inch speaker", "a 1x12 closed-back guitar cabinet with one 12-inch speaker"],
 "2x12": ["a 2x12 guitar speaker cabinet with two 12-inch speakers side by side", "a wide 2x12 guitar cab with two 12-inch speakers", "a 2x12 open-back guitar cabinet with two 12-inch speakers", "a 2x12 closed-back guitar cabinet with two 12-inch speakers", "a vertical 2x12 guitar cabinet with two 12-inch speakers stacked"],
 "4x12": ["a 4x12 guitar speaker cabinet with four 12-inch speakers in a square", "a tall 4x12 guitar cab with a slanted top half and four 12-inch speakers", "a straight-front 4x12 guitar cabinet with four 12-inch speakers", "a massive 4x12 guitar cabinet with four 12-inch speakers"],
 "1x10": ["a compact 1x10 bass speaker cabinet with one 10-inch speaker and a small bass port", "a small 1x10 bass cab with a single 10-inch driver and a horn tweeter"],
 "2x10": ["a 2x10 bass speaker cabinet with two 10-inch speakers and a horn tweeter", "a 2x10 bass cab with two 10-inch drivers and a slotted bass port", "a vertical 2x10 bass cabinet with two stacked 10-inch speakers"],
 "4x10": ["a 4x10 bass speaker cabinet with four 10-inch speakers and a horn tweeter", "a tall 4x10 bass cab with four 10-inch drivers and a slotted port", "a 4x10 bass cabinet with four 10-inch speakers in a square and a small horn"],
}
STYLE = ["photorealistic product photo", "cinematic photograph", "shallow depth-of-field studio product shot", "editorial photograph",
 "35mm film photograph with grain", "high-detail 3D render", "moody concert photograph", "vintage Kodachrome-style photograph",
 "hyperreal studio photograph", "polaroid-style photograph"]
ANGLE = ["straight-on front view", "three-quarter view from slightly above", "front view at eye level", "low-angle three-quarter view",
 "slight three-quarter angle", "front view, slightly from below"]
DETAIL = ["a leather handle on top", "a metal handle on top", "recessed side handles", "a strap handle on top", "visible screws around the speaker baffle",
 "piping around the baffle", "small rubber feet", "casters on the bottom", "a thin wooden baffle edge", "worn edges showing the wood underneath"]
SUFFIX = "whole cabinet in frame and centered, square composition, no logos, no brand names, no text or lettering anywhere"

def grill(open_bias=False):
    r = rng.random()
    if open_bias: r = r * 0.55 + 0.45
    if r < 0.42: return "cloth", rng.choice(CLOTH)
    if r < 0.52: return "tweed", rng.choice(TWEED)
    if r < 0.76: return "metal", rng.choice(METAL)
    return "open", rng.choice(OPEN_)

def make(size, genre, alu=False):
    g = GENRES[genre]
    kind, gr = grill(open_bias=alu)
    if alu and kind not in ("open", "metal"):
        kind, gr = "open", rng.choice(OPEN_)
    bass = size.endswith("x10")
    if kind == "tweed":
        cover, front = gr.split(" with ", 1)
        body = f"{rng.choice(LAYOUT[size])}, {cover}, {front}"
    else:
        body = f"{rng.choice(LAYOUT[size])}, covered in {rng.choice(g['cover'])}"
        if kind == "cloth":
            body += f", a front of {gr}"
        elif kind == "metal":
            cones = rng.choice(BASS_ALU if alu else (BASS_PAPER if bass else GUITAR_CONES))
            if size.startswith("1x"): cones = "a " + cones
            body += f", {gr} in front with {cones} visible behind it"
        else:
            cones = rng.choice(BASS_ALU if alu else (BASS_PAPER if bass else GUITAR_CONES))
            if size.startswith("1x"): cones = "a " + cones
            if size.startswith("1x"):
                gr = gr.replace("speakers", "speaker").replace("cones", "cone")
                cones = cones.replace("cones", "cone").replace("caps", "cap").replace("surrounds", "surround")
            body += f", {gr}, {cones}"
    if size.startswith("1x"):
        body = body.replace(" cones", " cone").replace("dust caps", "dust cap").replace("ribbed surrounds", "ribbed surround").replace("rubber surrounds", "rubber surround").replace("speakers exposed", "speaker exposed")
    body += f", {rng.choice(g['trim'])}, {rng.choice(DETAIL)}"
    return (f"{rng.choice(STYLE)} of {body}, {rng.choice(g['scene'])}, {rng.choice(g['light'])}, "
            f"{genre} vibe, {rng.choice(g['mood'])}, {rng.choice(ANGLE)}, {SUFFIX}"), kind, alu

plan = [("1x12", 45, False), ("2x12", 50, False), ("4x12", 55, False),
        ("1x10", 12, False), ("1x10", 4, True), ("2x10", 10, False), ("2x10", 6, True), ("4x10", 12, False), ("4x10", 6, True)]
out, seen = [], set()
for size, n, alu in plan:
    genres = BASS_GENRES if size.endswith("x10") else GUITAR_GENRES
    pool = []
    i = 0
    while i < n:
        if not pool: pool = genres[:]; rng.shuffle(pool)
        p, kind, a = make(size, pool.pop(), alu)
        if p in seen: continue
        seen.add(p); out.append((p, size, kind, a)); i += 1
rng.shuffle(out)
with open(sys.argv[1], "w") as f:
    for p, *_ in out: f.write(p + "\n")
from collections import Counter
print(len(out), Counter(s for _, s, _, _ in out), Counter(k for _, _, k, _ in out), sum(a for *_, a in out))
