# Pedal image prompts (Krea-2)

Every pedal has a photo made with Krea-2 (owner, 2026-10-28). Whenever the owner asks for a new pedal, write its image
prompts too, in the reply: four prompts, so the owner can render all four, keep the favourite and send it back. Then
add it with `tools/prepare_block_image.py` and place its face in `src/ui/PedalFaces.cpp` (see "After the render" below).

## The template

One prompt per line, filling in the slots in angle brackets:

> Top-down photo, camera directly above and facing straight down, of a **&lt;shape&gt;** guitar effects pedal flat on a
> dark stage floor the way it sits when played, not standing on end. Portrait 2:3 frame, the pedal filling nearly the
> whole frame, long axis vertical. Enclosure: **&lt;enclosure&gt;**. Top of the enclosure: a large plain smooth area left
> empty for knobs. Middle: a neon glyph in **&lt;glyph style&gt;**, glowing **&lt;colour&gt;**, showing **&lt;glyph&gt;**, lit
> from within. Below it: a smooth empty patch left clear for an indicator light. Bottom: a round latching stomp
> footswitch set into the enclosure. Two 1/4 inch instrument cables plugged into the jacks on the left and right sides,
> entering from the frame edges and taking little room. Indoors in a dark room, moody stage lighting, **&lt;lighting&gt;**,
> light haze. Photorealistic, sharp focus. No text, letters, numbers, labels, logos or brand names. Original design.

- **Glyph**: the pedal's own symbol, the same in all four prompts. A simple object or diagram that says what the pedal
  does (a transformer for a DI box, a bell for an FM synth, a snowflake for Freeze), described in a few words ("a
  transformer: two windings on an iron core"). It must differ from every other pedal's: look at
  `resources/blocks/` and at the pedal's symbol in `src/ui/BlockGlyphs.cpp`, which can be the starting point.
- **The four prompts** vary everything else, one pick from each list, a different one in each prompt.

| Shape | Enclosure | Glyph style | Lighting |
| --- | --- | --- | --- |
| rounded-corner rectangular | black powder-coated steel with a brushed aluminium rim | bold tube-style line-art | cool cyan and magenta rim light |
| slightly tapered-front | dark anodized aluminium with chamfered edges and countersunk screws | a solid silhouette with a soft glow | warm amber backlight |
| pill-ended | glossy black enamel with chrome screws and a chrome footswitch collar | fine hairline etch inside a thin neon ring | deep blue and violet spotlight |
| beveled-corner with raised side rails | textured dark sheet metal with raw edges and rubber feet at the corners | a minimal geometric reduction from straight segments and dots | red and teal side light |

Colours: cyan, lime green, violet, hot red, magenta, amber, ice blue, hot pink, mint, orange. Pick four different ones,
suited to the pedal (warm for drives, cool for reverbs and delays, and so on).

## Example (Studio DI)

```
Top-down photo, camera directly above and facing straight down, of a rounded-corner rectangular guitar effects pedal flat on a dark stage floor the way it sits when played, not standing on end. Portrait 2:3 frame, the pedal filling nearly the whole frame, long axis vertical. Enclosure: black powder-coated steel with a brushed aluminium rim. Top of the enclosure: a large plain smooth area left empty for knobs. Middle: a neon glyph in bold tube-style line-art, glowing cyan, showing a transformer: two windings on an iron core, lit from within. Below it: a smooth empty patch left clear for an indicator light. Bottom: a round latching stomp footswitch set into the enclosure. Two 1/4 inch instrument cables plugged into the jacks on the left and right sides, entering from the frame edges and taking little room. Indoors in a dark room, moody stage lighting, cool cyan and magenta rim light, light haze. Photorealistic, sharp focus. No text, letters, numbers, labels, logos or brand names. Original design.
Top-down photo, camera directly above and facing straight down, of a slightly tapered-front guitar effects pedal flat on a dark stage floor the way it sits when played, not standing on end. Portrait 2:3 frame, the pedal filling nearly the whole frame, long axis vertical. Enclosure: dark anodized aluminium with chamfered edges and countersunk screws. Top of the enclosure: a large plain smooth area left empty for knobs. Middle: a neon glyph in a solid silhouette with a soft glow, glowing lime green, showing a transformer: two windings on an iron core, lit from within. Below it: a smooth empty patch left clear for an indicator light. Bottom: a round latching stomp footswitch set into the enclosure. Two 1/4 inch instrument cables plugged into the jacks on the left and right sides, entering from the frame edges and taking little room. Indoors in a dark room, moody stage lighting, warm amber backlight, light haze. Photorealistic, sharp focus. No text, letters, numbers, labels, logos or brand names. Original design.
Top-down photo, camera directly above and facing straight down, of a pill-ended guitar effects pedal flat on a dark stage floor the way it sits when played, not standing on end. Portrait 2:3 frame, the pedal filling nearly the whole frame, long axis vertical. Enclosure: glossy black enamel with chrome screws and a chrome footswitch collar. Top of the enclosure: a large plain smooth area left empty for knobs. Middle: a neon glyph in fine hairline etch inside a thin neon ring, glowing violet, showing a transformer: two windings on an iron core, lit from within. Below it: a smooth empty patch left clear for an indicator light. Bottom: a round latching stomp footswitch set into the enclosure. Two 1/4 inch instrument cables plugged into the jacks on the left and right sides, entering from the frame edges and taking little room. Indoors in a dark room, moody stage lighting, deep blue and violet spotlight, light haze. Photorealistic, sharp focus. No text, letters, numbers, labels, logos or brand names. Original design.
Top-down photo, camera directly above and facing straight down, of a beveled-corner with raised side rails guitar effects pedal flat on a dark stage floor the way it sits when played, not standing on end. Portrait 2:3 frame, the pedal filling nearly the whole frame, long axis vertical. Enclosure: textured dark sheet metal with raw edges and rubber feet at the corners. Top of the enclosure: a large plain smooth area left empty for knobs. Middle: a neon glyph in a minimal geometric reduction from straight segments and dots, glowing hot red, showing a transformer: two windings on an iron core, lit from within. Below it: a smooth empty patch left clear for an indicator light. Bottom: a round latching stomp footswitch set into the enclosure. Two 1/4 inch instrument cables plugged into the jacks on the left and right sides, entering from the frame edges and taking little room. Indoors in a dark room, moody stage lighting, red and teal side light, light haze. Photorealistic, sharp focus. No text, letters, numbers, labels, logos or brand names. Original design.
```

A pedal played with the foot (volume, wah, whammy) may instead be described as an expression pedal with a treadle; Krea
drew those three that way anyway, with the glyph and indicator below the treadle.

## After the render

1. `python3 tools/prepare_block_image.py <render.png> <typeId> --box x0,y0,x1,y1 --preview /tmp/face.png`: the box is
   the enclosure, as fractions of the render (Krea seldom makes the pedal fill the frame). It writes
   `resources/blocks/<typeId>.webp` and its thumbnail; the build embeds them.
2. In `src/ui/PedalFaces.cpp`, from the preview's 5 % grid (fractions of the cropped image): 2-3 knobs for the pedal's
   most important controls in the empty top area (a row; radius about 0.1 for three, 0.11 for two), the power LED on
   the indicator spot north of the footswitch, coloured like the glyph, the footswitch (a treadle's toe), and the
   knobs' rim colour, the glyph's colour (all zero for black knobs). `tests/PedalFaceTests.cpp` checks they fit
   and don't overlap.

## Waiting for renders

New pedals drawn with the placeholder art until their render arrives (`tests/PedalFaceTests.cpp` lists them as
awaiting a photo; take a pedal off that list when its image and face are added). None right now.
