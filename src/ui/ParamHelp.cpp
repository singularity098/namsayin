// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "ParamHelp.hpp"

#include "engine/Params.hpp"

#include <cstring>

namespace namsayin {

namespace {

struct BlockHelp {
    const char* typeId;
    const char* param;
    const char* help;
};

// Shared wording for controls many blocks have
#define MIX_HELP(what)                                                                                                \
    "Blends the " what " with your dry signal. 0% is dry only and 100% is the effect only; around 50% the two are "   \
    "equal. Lower it for a subtle touch, raise it for a bigger, more obvious effect."
#define SYNC_HELP(knob)                                                                                               \
    "Locks the " knob " to your DAW's tempo. When it's on, the " knob " knob picks a note value (like 1/4 or a "      \
    "dotted 1/8) instead of a free value, and follows tempo changes. Without a DAW tempo it assumes 120 BPM."
#define WINDOW_HELP                                                                                                   \
    "The length of the slices the pitch shifter works with. Shorter windows react faster and trail your playing "    \
    "less, but can sound grainy on low notes. Longer windows sound smoother on low notes and chords, but the "      \
    "shifted sound lags a little behind. Start around 40-50 ms."
#define WIDTH_HELP(what)                                                                                              \
    "How wide the " what " spreads across the stereo field. 0% keeps both sides the same (centred); 100% makes "    \
    "left and right move differently for a wide, swirling sound. Only works when the plugin is in Stereo mode."

constexpr BlockHelp kBlockHelp[] = {
    // Local NAM
    { "nam", "Input",
      "How hard you hit the amp model. Turning it up drives the model harder, so you get more gain, compression and "
      "distortion, just like turning up the guitar's volume into a real amp; turning it down cleans it up. Set it so "
      "your playing gets the amount of grit you want, then match the loudness with Output." },
    { "nam", "Output",
      "The volume coming out of the model, without changing its tone. Use it to balance this block against the rest "
      "of the chain, or to match the loudness when you change Input. 0 dB leaves the level as it is." },
    { "nam", "Normalize",
      "Brings every model to about the same loudness, using the loudness value stored in the .nam file. Keep it on "
      "so switching between models doesn't make big jumps in volume. Models without a stored loudness are left as "
      "they are." },

    // Local IR
    { "ir", "Level",
      "The volume of the cab after the impulse response, without changing its tone. Use it to balance this cab "
      "against the rest of the chain. 0 dB leaves the level as it is." },
    { "ir", "Mix", MIX_HELP("cab sound") " Most of the time you want 100% for a cab; lower values let some of the raw "
                   "amp sound through." },
    { "ir", "Low Cut",
      "Removes the lowest frequencies after the cab. Turn it up to tighten a boomy or muddy cab, often to 80-120 Hz "
      "for a band mix. All the way down (20 Hz) it's off." },
    { "ir", "High Cut",
      "Removes the highest frequencies after the cab. Turn it down to soften fizz or harshness, often to 6-10 kHz. "
      "All the way up (20 kHz) it's off." },
    { "ir", "Normalize",
      "Brings every IR to about the same loudness, so switching cabs doesn't make big jumps in volume. Turn it off "
      "to hear the file at its own level." },

    // Boost
    { "boost", "Gain",
      "How much louder the boost makes your signal, from 0 to 24 dB. Before an amp, a boost pushes it into more "
      "drive and tightens the sound; after an amp, it's a solo volume boost. Try 6-10 dB in front of a dirty amp." },
    { "boost", "Type",
      "The flavour of the boost. Clean raises everything evenly. Treble boosts the highs, like a vintage treble "
      "booster for cutting leads. Mid pushes the middle, which helps a solo sit on top of the band." },
    { "boost", "Tone",
      "Tilts the sound darker or brighter. 50% is flat; below that the boost is darker, above it brighter." },

    // Overdrive
    { "overdrive", "Drive",
      "How much the pedal distorts. Low settings add a little warmth and grit that cleans up when you play softly; "
      "high settings give a thick, singing overdrive. Into an already dirty amp, keep it low and use Level to push "
      "the amp instead." },
    { "overdrive", "Tone",
      "Brightness of the drive. Turn it down for a smoother, darker sound and up for more bite and presence." },
    { "overdrive", "Level",
      "The pedal's output volume. Set it so the pedal is as loud as bypass, or louder to push the amp after it." },
    { "overdrive", "Type",
      "The character of the drive. Screamer has the famous mid hump and keeps your clean signal underneath. Blues is "
      "flatter and more open. Transparent keeps your guitar's natural tone and only adds gain as Drive goes up." },
    { "overdrive", "Mix", MIX_HELP("overdrive") },

    // Distortion
    { "distortion", "Drive",
      "How much distortion. Low settings give a crunchy rhythm sound; high settings give a saturated lead or "
      "high-gain rhythm sound with lots of sustain." },
    { "distortion", "Tone",
      "Brightness of the distortion. With the RAT type it works like the original's Filter, darkening the sound as "
      "you turn it down. Turn it up for more cut, down to tame fizz." },
    { "distortion", "Level",
      "The pedal's output volume. Distortion pedals often get louder as you add Drive; use this to match the level "
      "with bypass." },
    { "distortion", "Type",
      "The kind of distortion. RAT is a raw, hard-clipped classic that gets darker as Drive goes up. DS-1 is bright "
      "and scooped. Metal has two gain stages, tight lows and scooped mids for heavy riffing." },

    // Fuzz
    { "fuzz", "Fuzz",
      "How much fuzz. Low settings are a raspy, broken-up sound; high settings are thick, woolly and sustain for a "
      "long time. Fuzz pedals clean up nicely if you turn down your guitar's volume." },
    { "fuzz", "Tone",
      "Brightness of the fuzz. Turn it down for a dark, woolly sound and up for a buzzy, cutting one." },
    { "fuzz", "Level",
      "The pedal's output volume. Fuzz pedals can be much louder or quieter than bypass depending on the Fuzz "
      "setting; set this to match bypass, or louder for a solo." },
    { "fuzz", "Type",
      "The kind of fuzz. Face is a vintage germanium fuzz, warm and touch sensitive. Muff is a thick, sustaining "
      "fuzz with scooped mids. Octave adds a ringing octave up, especially high on the neck." },
    { "fuzz", "Bias",
      "Changes how the fuzz's transistor is set up. 50% is normal. Lower settings starve it, so notes break up into "
      "a gated, sputtering sound as they decay; higher settings change the texture of the clipping." },

    // Bitcrusher
    { "bitcrusher", "Bits",
      "How many bits the sound is reduced to. 16 is clean; fewer bits add more grit and noise, down to a harsh, "
      "broken sound at 1-4. Try 6-8 for a lo-fi video-game texture." },
    { "bitcrusher", "Rate",
      "The sample rate the sound is reduced to. Lower rates add metallic, ringing overtones (aliasing) and lose the "
      "highs, like an old sampler. Try 4-8 kHz for an obvious lo-fi sound." },
    { "bitcrusher", "Tone",
      "A filter after the crushing that softens its harsh highs. Turn it down to tame the fizz. All the way up it's "
      "off." },
    { "bitcrusher", "Mix", MIX_HELP("crushed sound") },

    // Compressor
    { "compressor", "Threshold",
      "The level where the compressor starts turning things down. Anything louder than this gets squeezed. The "
      "lower you set it, the more of your playing is compressed; watch the Reduction meter to see how much it's "
      "working." },
    { "compressor", "Ratio",
      "How strongly it squeezes the parts above the Threshold. 2:1 is gentle, 4:1 is a firm, even sound, and 10:1 "
      "or more is close to a limiter. Higher ratios give more sustain and a more squashed sound." },
    { "compressor", "Attack",
      "How fast it reacts to a loud note. Fast attack catches the pick attack and smooths it; slower attack (10-30 "
      "ms) lets the pick snap through before turning down, which keeps the sound punchy." },
    { "compressor", "Release",
      "How fast it lets go after the level drops. Short release recovers quickly but can pump; long release is "
      "smoother. 100-300 ms suits most guitar parts." },
    { "compressor", "Knee",
      "How gradually compression starts around the Threshold. 0 dB is a hard knee that clamps down suddenly; higher "
      "values ease in gently for a more natural sound." },
    { "compressor", "Makeup",
      "Adds volume back after compressing, since squeezing makes things quieter. Raise it until the block is about "
      "as loud as bypass." },
    { "compressor", "Mix",
      MIX_HELP("compressed sound") " Lower settings are parallel compression: sustain and evenness while keeping "
      "your natural pick attack." },

    // Noise Gate
    { "gate", "Threshold",
      "The level below which the gate closes and silences the sound. Set it just above the hiss and hum you hear "
      "when you're not playing. Too high and it cuts off the end of your notes." },
    { "gate", "Reduction",
      "How much the sound is turned down when the gate is closed. 80-90 dB is near silence; 10-20 dB just lowers "
      "the noise, which sounds more natural." },
    { "gate", "Attack",
      "How fast the gate opens when you start playing. Keep it short (around 1 ms) so the pick attack isn't lost." },
    { "gate", "Hold",
      "How long the gate stays open after the level drops below the Threshold, before it starts closing. A little "
      "hold stops it from chattering on and off during short gaps." },
    { "gate", "Release",
      "How fast the gate closes once it starts. Short release cuts tightly, good for staccato metal; longer release "
      "lets notes fade out naturally." },
    { "gate", "Hysteresis",
      "How far the level has to fall below the Threshold before the gate closes again. A few dB stops the gate from "
      "flickering open and closed on notes hovering around the Threshold." },
    { "gate", "Type",
      "Gate closes all the way (by Reduction) when you stop playing. Expander turns quiet sounds down gradually, "
      "the further below the Threshold they are, which is more natural." },
    { "gate", "Key",
      "What the gate listens to. Block In listens to the sound coming into this block. Rig In listens to your clean "
      "guitar before the whole chain, so a gate placed after a high-gain amp follows your real playing, not the "
      "amp's noise." },

    // Volume
    { "volume", "Volume",
      "The volume, from silence (0%) to full (100%). Map an expression pedal to it with MIDI learn (right-click the "
      "knob) to use it as a volume pedal for swells and fades." },
    { "volume", "Taper",
      "How the knob's travel maps to loudness. Audio feels even all the way through, like a real volume pedal. "
      "Linear stays quiet longer and comes in late, which suits slow swells." },

    // Auto Swell
    { "swell", "Attack",
      "How long each note takes to fade in. Short times soften the pick attack; long times (1 s or more) give a "
      "violin-like or pad-like swell." },
    { "swell", "Threshold",
      "How loud a note has to be to start a new swell. Set it above the noise but below your softest notes. If "
      "swells don't start, lower it; if noise triggers them, raise it." },
    { "swell", "Retrigger",
      "When on, a fresh pick attack starts a new swell even while the last note is still ringing. Turn it off so "
      "only notes played after a pause swell." },

    // Graphic EQ
    { "eq.graphic", "100",
      "Boosts or cuts the deep lows around 100 Hz: the thump and weight. Cut it to tighten a boomy sound; boost for "
      "more body." },
    { "eq.graphic", "200",
      "Boosts or cuts the low mids around 200 Hz: warmth and fullness. Too much sounds muddy; cutting it clears the "
      "mix." },
    { "eq.graphic", "400",
      "Boosts or cuts around 400 Hz, where boxy and wooden sounds live. Cut it to open the sound up." },
    { "eq.graphic", "800",
      "Boosts or cuts the mids around 800 Hz: the core of the guitar's sound. Boost it to cut through a band mix, "
      "cut it for a scooped sound." },
    { "eq.graphic", "1.6k",
      "Boosts or cuts the upper mids around 1.6 kHz: bite, attack and presence. A small boost helps a solo stand "
      "out; too much sounds honky." },
    { "eq.graphic", "3.2k",
      "Boosts or cuts around 3.2 kHz: clarity and edge. Cut it if the sound is harsh or hurts the ears." },
    { "eq.graphic", "6.4k",
      "Boosts or cuts the highs around 6.4 kHz: sparkle and air, and also fizz. Cut it to smooth a fizzy distortion." },
    { "eq.graphic", "Level",
      "Overall volume after the EQ. Boosting bands makes things louder; use this to match the level with bypass." },

    // Parametric EQ
    { "eq.parametric", "Low Cut",
      "Removes everything below this frequency. Use it to get rid of rumble and boom, often 60-100 Hz. All the way "
      "down (20 Hz) it's off." },
    { "eq.parametric", "Low",
      "Raises or lowers everything below about 100 Hz, like a bass knob. Boost for weight, cut to tighten." },
    { "eq.parametric", "Mid Freq",
      "Chooses which frequency the Mid knob works on. Sweep it while boosting Mid to find the spot you want, then "
      "set Mid to taste: around 400 Hz is boxy, 800 Hz is body, 1.5-2 kHz is bite." },
    { "eq.parametric", "Mid",
      "Boosts or cuts the band at Mid Freq. Boost to bring the guitar forward, cut to remove an unpleasant spot." },
    { "eq.parametric", "Q",
      "How narrow the Mid band is. Low values affect a broad, musical range; high values hit a narrow spot, which is "
      "good for cutting a single harsh or ringing frequency." },
    { "eq.parametric", "High",
      "Raises or lowers everything above about 4 kHz, like a treble knob. Boost for sparkle, cut to soften." },
    { "eq.parametric", "High Cut",
      "Removes everything above this frequency. Use it to remove fizz and harshness, often 6-10 kHz. All the way up "
      "(20 kHz) it's off." },
    { "eq.parametric", "Level",
      "Overall volume after the EQ. Use it to match the level with bypass after boosting or cutting." },

    // Chorus
    { "chorus", "Rate",
      "How fast the chorus sweeps. Slow rates (0.3-1 Hz) are lush and gentle; fast rates become a wobbly vibrato or "
      "Leslie-like shimmer." },
    { "chorus", "Depth",
      "How far the pitch wavers. Low depth is a subtle thickening; high depth is a seasick, detuned sound." },
    { "chorus", "Mix", MIX_HELP("chorus") " For a classic chorus, 50% is the sweet spot." },
    { "chorus", "Type",
      "The kind of chorus. Classic is one voice, like a vintage chorus pedal. Dimension is two subtle voices for a "
      "wide, still sound. Tri is three voices for a big, lush ensemble. Vibrato is pitch wobble only, no dry "
      "signal." },
    { "chorus", "Tone",
      "Darkens the chorused sound. Turn it down for a warmer, more vintage chorus. All the way up it's off." },
    { "chorus", "Width", WIDTH_HELP("chorus") },

    // Flanger
    { "flanger", "Rate",
      "How fast the flanger sweeps up and down. Slow rates give the classic jet-plane whoosh; fast rates a wobbling "
      "warble." },
    { "flanger", "Sync", SYNC_HELP("rate") },
    { "flanger", "Depth",
      "How far the sweep travels. At 0% it doesn't move, leaving a fixed comb-filter tone set by Manual; at 100% it "
      "sweeps widely." },
    { "flanger", "Manual",
      "The centre of the sweep. Short times give a high, metallic sweep; longer times a lower, deeper one. With "
      "Depth at 0%, sweep this knob by hand (or with MIDI) for a manual flange." },
    { "flanger", "Feedback",
      "How intense and resonant the flanging is. Positive values give the classic jet sound; negative values a "
      "hollow, nasal one. Near the extremes it rings like metal." },
    { "flanger", "Mix", MIX_HELP("flanged sound") " 50% gives the deepest notches." },
    { "flanger", "Width", WIDTH_HELP("flanger") },

    // Phaser
    { "phaser", "Rate",
      "How fast the phaser sweeps. Slow rates are a gentle swoosh; fast rates a bubbly, rotary-like sound." },
    { "phaser", "Sync", SYNC_HELP("rate") },
    { "phaser", "Depth", "How far the sweep travels. Low depth is subtle; high depth a big, obvious swirl." },
    { "phaser", "Center",
      "The middle of the sweep. Low settings make it throaty, high settings make it thin and airy." },
    { "phaser", "Feedback",
      "How resonant and vocal the phasing sounds. Higher values make the sweep more pronounced and whistling; "
      "negative values change its character." },
    { "phaser", "Mix", MIX_HELP("phased sound") " 50% gives the strongest phasing; with Vibe at 100% you get its "
                       "vibrato." },
    { "phaser", "Type",
      "How many stages the phaser has. 4 Stage is the classic subtle swirl; 6, 8 and 12 stages give more and deeper "
      "notches. Vibe imitates a Uni-Vibe: a throbbing, liquid sound." },
    { "phaser", "Width", WIDTH_HELP("phaser") },

    // Tremolo
    { "tremolo", "Rate",
      "How fast the volume pulses. Slow rates give a gentle wave; fast rates a choppy, helicopter-like stutter." },
    { "tremolo", "Sync", SYNC_HELP("rate") " The loudest point of each pulse lands on the beat while the DAW plays." },
    { "tremolo", "Depth",
      "How much the volume dips. Low depth is a gentle shimmer; 100% chops all the way to silence." },
    { "tremolo", "Type",
      "The shape of the pulse. Sine is smooth, Triangle is even, Square is choppy. Opto is the warm, slightly "
      "uneven pulse of a tube amp's tremolo. Harmonic pulses the lows and highs in turns, like a vintage Fender. Pan "
      "moves the sound left and right in Stereo mode." },
    { "tremolo", "Level", "Output volume of the block. Use it to make up for the volume the tremolo takes away." },

    // Rotary
    { "rotary", "Speed",
      "Switches the rotating speaker between Slow (chorale) and Fast (tremolo). The change speeds up or slows down "
      "gradually, like the real thing. Map a footswitch to it with MIDI learn." },
    { "rotary", "Slow",
      "How fast the horn spins at the Slow setting. The classic slow (chorale) speed is under 1 Hz: a gentle, "
      "swirling chorus-like sound." },
    { "rotary", "Fast",
      "How fast the horn spins at the Fast setting. The classic fast speed is around 6-7 Hz: a fast, throbbing "
      "tremolo with a strong pitch wobble." },
    { "rotary", "Ramp",
      "How long the speakers take to speed up or slow down when you switch Speed. 100% is the few seconds of a real "
      "Leslie; lower is quicker." },
    { "rotary", "Drive",
      "Overdrives the amp in front of the rotating speaker. A little adds warmth; a lot gives the growly sound "
      "of a cranked organ." },
    { "rotary", "Balance",
      "The level of the high horn against the low drum. Raise it for a brighter, more swirly sound, lower it for a "
      "darker, more throbbing one." },
    { "rotary", "Width",
      "How far apart the two virtual microphones are, so how wide the stereo image is. Only works in Stereo mode." },
    { "rotary", "Mix", MIX_HELP("rotary sound") },

    // Doubler
    { "doubler", "Voices",
      "How many extra takes are added. Single adds one double, like a second guitar track. Double adds two, panned "
      "left and right in Stereo mode, for a big wall of guitars." },
    { "doubler", "Delay",
      "How far behind your playing the doubles are. 10-25 ms sounds like a tight second take; longer times start to "
      "sound like a slapback." },
    { "doubler", "Drift",
      "How much the doubles wander in timing and pitch, like a real second performance. 0% is a static, robotic "
      "copy; around 40% sounds natural; high values sound loose and wobbly." },
    { "doubler", "Tone",
      "Darkens the doubles so they sit slightly behind your main part instead of competing with it, like a "
      "second take played on a warmer guitar. All the way up it's off." },
    { "doubler", "Width",
      "How far the doubles are panned to the sides. Works in Stereo mode; best after the amp and cab." },
    { "doubler", "Mix", MIX_HELP("doubles") },

    // Ring Mod
    { "ringmod", "Frequency",
      "The pitch of the tone your guitar is multiplied with. It creates new, unrelated pitches: low settings give a "
      "warbling tremolo, higher ones bell-like, robotic or clangy sounds." },
    { "ringmod", "Mix", MIX_HELP("ring-modulated sound") },
    { "ringmod", "Rate", "How fast the LFO sweeps the Frequency up and down (set Depth above 0% to hear it)." },
    { "ringmod", "Depth",
      "How far the LFO sweeps the Frequency. 0% keeps it fixed; 100% sweeps an octave up and down, for sci-fi "
      "sounds." },
    { "ringmod", "Wave",
      "The shape of the tone your guitar is multiplied with. Sine is smooth and bell-like; Square is harsher "
      "and buzzier, with more new pitches." },

    // Reverb
    { "reverb", "Type",
      "The kind of space. Room is a small, natural space. Hall is a big, smooth concert hall. Plate is the bright, "
      "dense studio reverb heard on many records." },
    { "reverb", "Decay",
      "How long the reverb takes to die away, in seconds. Short decays add space without washing things out; long "
      "decays give huge ambient tails." },
    { "reverb", "Pre-Delay",
      "A short gap before the reverb starts. A little pre-delay (10-40 ms) keeps your notes clear in front of the "
      "reverb." },
    { "reverb", "Damping",
      "How quickly the high frequencies fade in the tail. More damping sounds warmer and darker, like a room full "
      "of soft surfaces; less is brighter." },
    { "reverb", "Low Cut",
      "Removes lows from the reverb only, so the tail doesn't muddy your sound. All the way down it's off." },
    { "reverb", "High Cut",
      "Removes highs from the reverb only, for a darker, smoother tail. All the way up it's off." },
    { "reverb", "Width", "How wide the reverb tail is. 100% is fully wide; 0% is centred. Stereo mode only." },
    { "reverb", "Mix", MIX_HELP("reverb") " 15-30% adds space; higher settings push the guitar far back." },

    // Spring Reverb
    { "reverb.spring", "Decay",
      "How long the springs ring on, in seconds. Short decays give a tight, slappy reverb; long decays a big, "
      "washy surf sound." },
    { "reverb.spring", "Tension",
      "How tight the springs are. Tighter springs sound shorter and brighter; looser ones darker and longer." },
    { "reverb.spring", "Drip",
      "How strongly the springs chirp and 'drip'. High settings give the wet, surfy boing of a vintage amp's spring "
      "tank." },
    { "reverb.spring", "Tone",
      "Brightness of the springs. Turn it down for a darker, more vintage reverb; turn it up for a bright, "
      "splashy one." },
    { "reverb.spring", "Dwell",
      "How hard the springs are driven, like the Dwell knob of a surf reverb unit. More dwell gives a more intense, "
      "splashier reverb." },
    { "reverb.spring", "Mix", MIX_HELP("spring reverb") " For surf, go 40% and up." },
    { "reverb.spring", "Width",
      "How wide the two springs spread across the stereo field. 0% is centred; higher settings sound wider. "
      "Stereo mode only." },

    // Shimmer
    { "shimmer", "Decay",
      "How long the reverb tail lasts, in seconds. Short decays keep the shimmer subtle; long decays build "
      "huge, ambient pads that keep rising." },
    { "shimmer", "Shimmer",
      "How much pitch-shifted sound is fed back into the reverb. More shimmer builds a rising, choir-like halo; 0% "
      "is a plain reverb." },
    { "shimmer", "Interval",
      "How far the shimmer is shifted. +1 Oct is the classic angelic shimmer; +5th and +Oct+5th give an organ-like "
      "chord; -1 Oct gives a dark, droning tail." },
    { "shimmer", "Tone",
      "Brightness of the tail. Turn it down if the shimmer gets too glassy or piercing; turn it up for more "
      "sparkle." },
    { "shimmer", "Size",
      "How big the space sounds. Small sizes are more intimate; larger sizes give a slower, more spread-out "
      "tail." },
    { "shimmer", "Low Cut",
      "Removes the lows from the reverb only, so the tail doesn't muddy your playing. Raise it for an airier, "
      "more ethereal shimmer." },
    { "shimmer", "Mix", MIX_HELP("shimmer reverb") },

    // Freeze
    { "freeze", "Freeze",
      "Captures the sound you just played and holds it forever as a pad, so you can play over it. Turn it off and "
      "the pad fades away. Map a footswitch to it with MIDI learn." },
    { "freeze", "Layer",
      "While on (and frozen), whatever you play is added into the held pad, so you can build up chords and layers." },
    { "freeze", "Fade In",
      "How quickly the held pad fades in when you freeze. Short times capture the sound almost instantly; long "
      "times bring the pad in slowly and smoothly, like a swell." },
    { "freeze", "Fade Out",
      "How slowly the held pad fades away when you turn Freeze off. Short times stop it quickly; long times let "
      "it die away gently under your playing." },
    { "freeze", "Tone",
      "Darkens the held pad so it sits behind your playing instead of clashing with it. Turn it down for a "
      "soft, warm drone. All the way up it's off." },
    { "freeze", "Level",
      "The volume of the held pad. Lower it so the pad sits quietly underneath what you play. Your dry playing "
      "always passes at full level." },

    // Delay
    { "delay", "Time",
      "The time between repeats. Short times (60-150 ms) give a slapback; 300-500 ms a classic echo; longer times "
      "spacious ambient repeats. With Sync on, it picks a note value instead." },
    { "delay", "Sync", SYNC_HELP("time") },
    { "delay", "Feedback",
      "How many repeats you hear. Low settings give one or two echoes; high settings many; near 100% the repeats "
      "go on and on." },
    { "delay", "Mix", MIX_HELP("echoes") " 20-35% keeps the repeats behind your playing." },
    { "delay", "Tone",
      "Darkens each repeat, so the echoes get warmer as they fade, like an analog delay. All the way up the "
      "repeats stay bright." },
    { "delay", "Mode",
      "Mono repeats in the centre. Stereo uses separate left and right delays (offset by Spread). Ping-Pong "
      "bounces the repeats from left to right. Stereo and Ping-Pong need Stereo mode." },
    { "delay", "Spread",
      "Delays the right side a little more than the left, for a wide, spacious echo. Stereo and Ping-Pong modes." },
    { "delay", "Mod",
      "Adds a gentle wobble to the repeats, like a tape or analog delay, for a lusher sound. 0% is clean digital "
      "repeats." },

    // Doppelganger Delay
    { "doppelganger", "Time",
      "The time between echoes. Echoes alternate between flanged and chorused, so longer times make the alternation "
      "easier to hear. With Sync on, it picks a note value instead." },
    { "doppelganger", "Sync", SYNC_HELP("time") },
    { "doppelganger", "Feedback",
      "How many echoes you hear. Higher settings give more repeats, alternating between the two effects." },
    { "doppelganger", "Mix", MIX_HELP("echoes") },
    { "doppelganger", "Tone",
      "Darkens each repeat, so the echoes get warmer as they fade. All the way up it's off." },
    { "doppelganger", "Order",
      "Which effect comes first. Flange 1st makes the 1st, 3rd, 5th echoes flanged and the 2nd, 4th, 6th chorused; "
      "Chorus 1st swaps them." },
    { "doppelganger", "Width",
      "How differently the effects move on the left and right sides, for a wider sound. Stereo mode only." },
    { "doppelganger", "Bounce",
      "Leans the flanged echoes to the left and the chorused echoes to the right, so they bounce across the stereo "
      "field. Stereo mode only." },
    { "doppelganger", "Flange Rate",
      "How fast the flanger on the flanged echoes sweeps up and down. Slow rates give a slow jet-plane whoosh "
      "across the echoes; fast rates a quick warble." },
    { "doppelganger", "Flange Depth",
      "How far the flanger on the flanged echoes sweeps. Low depth gives a subtle metallic colour; high depth a "
      "big, obvious whoosh on each flanged echo." },
    { "doppelganger", "Flange Regen",
      "How intense the flanger is: higher values give a thicker, jet-like flange; negative values a hollow one." },
    { "doppelganger", "Flange Mix",
      "How much flanger is on the flanged echoes. 100% gives the full flange; 0% makes those echoes plain, "
      "clean repeats, so only the chorused echoes carry an effect." },
    { "doppelganger", "Chorus Rate",
      "How fast the chorus on the chorused echoes sweeps. Slow rates (0.5-1 Hz) are lush and gentle; faster "
      "rates sound wobblier, like a vibrato." },
    { "doppelganger", "Chorus Depth",
      "How much the chorus on the chorused echoes wavers in pitch. Low depth is a gentle thickening; high depth "
      "is a lush, seasick shimmer." },
    { "doppelganger", "Voices",
      "How many chorus voices the chorused echoes have. More voices sound thicker and lusher." },
    { "doppelganger", "Chorus Mix",
      "How much chorus is on the chorused echoes. 100% gives the full chorus; 0% makes those echoes plain, "
      "clean repeats, so only the flanged echoes carry an effect." },

    // Tape Echo
    { "tapeecho", "Time",
      "The time to the first playback head. The other heads repeat at two and three times this. With Sync on, it "
      "picks a note value instead. Turning it while playing bends the repeats' pitch, like changing tape speed." },
    { "tapeecho", "Sync", SYNC_HELP("time") },
    { "tapeecho", "Feedback",
      "How many repeats you hear. High settings make the tape echo build up and run away into self-oscillation." },
    { "tapeecho", "Mix", MIX_HELP("echoes") },
    { "tapeecho", "Tone",
      "How dark the repeats are. Tape loses highs on every pass; turn it down for a murkier, more vintage sound." },
    { "tapeecho", "Wow",
      "How much the tape speed wavers, which bends the repeats' pitch. A little sounds warm and vintage; a lot "
      "sounds like a worn-out machine." },
    { "tapeecho", "Drive",
      "How hard the tape is saturated. A little drive gives warm, slightly compressed repeats; more gives "
      "fuzzy, gritty repeats that break up as they build with Feedback." },
    { "tapeecho", "Heads",
      "Which playback heads are on. Each head repeats at a different time (1x, 2x and 3x Time); combining heads "
      "gives rhythmic patterns of echoes." },

    // Ducking Delay
    { "duckdelay", "Time",
      "The time between repeats. Short times give a slapback, 300-500 ms a classic echo, longer times spacious "
      "repeats. With Sync on, it picks a note value instead." },
    { "duckdelay", "Sync", SYNC_HELP("time") },
    { "duckdelay", "Feedback",
      "How many repeats you hear. Low settings give one or two echoes; high settings a long trail of repeats, "
      "which still stay out of the way while you play." },
    { "duckdelay", "Mix", MIX_HELP("echoes") },
    { "duckdelay", "Tone", "Darkens each repeat, so the echoes get warmer as they fade. All the way up it's off." },
    { "duckdelay", "Threshold",
      "How loud your playing has to be to push the repeats down. Lower settings duck the echoes more often." },
    { "duckdelay", "Duck",
      "How much the repeats are turned down while you play. They come back up in the gaps, so they never crowd "
      "your notes." },
    { "duckdelay", "Release",
      "How quickly the repeats swell back up after you stop playing. Short releases bring them back almost at "
      "once; long releases make them fade in slowly, like a swell." },

    // Looper
    { "looper", "Rec/Dub",
      "Press to record. Press again to close the loop and play it back. While it plays, press to overdub more "
      "layers, and again to go back to just playing. Map a footswitch to it with MIDI learn." },
    { "looper", "Play/Stop",
      "Stops the loop, or starts it again from the beginning. While recording, it closes the loop and plays it." },
    { "looper", "Clear",
      "Erases the loop completely, so you can record a new one. There's no undo for this, so be sure you're "
      "done with the loop. Map a footswitch to it with MIDI learn." },
    { "looper", "Level",
      "The volume of the loop playback. Lower it so the loop sits behind what you play live. Your live playing "
      "always passes at full level." },
    { "looper", "Feedback",
      "How much of the loop is kept on each overdub pass. 100% keeps everything; lower values fade old layers out "
      "as you add new ones." },
    { "looper", "Reverse",
      "Plays the loop backwards, for a swelling, otherworldly sound. Turn it off to play the loop forwards "
      "again." },
    { "looper", "Half Speed",
      "Plays the loop at half speed, so it sounds an octave lower and twice as long. Great for a slowed-down, "
      "dreamy version of what you just played." },

    // Octave
    { "octave", "Sub 1",
      "Level of the voice one octave below what you play, for a fat, bass-like sound. Play single notes; chords "
      "confuse it." },
    { "octave", "Sub 2", "Level of the voice two octaves below what you play, for a deep, synth-like bass." },
    { "octave", "Up", "Level of the voice one octave above what you play, for a ringing, fuzzy octave-up." },
    { "octave", "Dry",
      "The level of your original signal. Keep some dry for a big, doubled sound; turn it all the way down to "
      "hear only the octave voices, like a bass guitar." },
    { "octave", "Tone",
      "Darkens the octave voices so they sound smoother and rounder. Turn it down for a deep, synth-bass sound, "
      "up for more growl." },
    { "octave", "Tracking",
      "Helps the detector find your note. If the octaves glitch or flip, lower it for low notes or raise it for "
      "higher notes." },

    // Poly Octave
    { "polyoctave", "Sub 2",
      "The level of the voice two octaves below what you play, for a deep, rumbling bass. Works with chords "
      "too." },
    { "polyoctave", "Sub 1",
      "The level of the voice one octave below what you play, for an organ-like or bass-doubled sound. Works "
      "with chords too." },
    { "polyoctave", "Up 1",
      "The level of the voice one octave above what you play, for a 12-string guitar or organ-like shimmer. "
      "Works with chords too." },
    { "polyoctave", "Up 2",
      "The level of the voice two octaves above what you play, for a bright, glassy, bell-like sound. Works "
      "with chords too." },
    { "polyoctave", "Dry",
      "The level of your original signal. Keep it up and add octaves for a bigger sound, or turn it down for "
      "octave voices only." },
    { "polyoctave", "Tone",
      "Darkens the octave voices, which smooths the up octaves and makes the sub octaves rounder. All the way "
      "up it's off." },
    { "polyoctave", "Window", WINDOW_HELP },

    // Pitch Shifter
    { "pitch", "Shift 1", "How many semitones voice 1 is shifted, up to two octaves up or down. 12 is an octave." },
    { "pitch", "Level 1",
      "The volume of voice 1. Balance it against Dry: equal levels give an even harmony, a lower level a subtle "
      "one. At 0% voice 1 is off." },
    { "pitch", "Shift 2",
      "How many semitones voice 2 is shifted, up to two octaves up or down. 12 is an octave and 7 a fifth. Try "
      "+4 with voice 1 at +7 for a major chord." },
    { "pitch", "Level 2",
      "The volume of voice 2. At 0% voice 2 is off; raise it to add a second shifted voice, for example to make "
      "a chord with voice 1." },
    { "pitch", "Detune",
      "Shifts voice 1 slightly up and voice 2 slightly down, in cents. With the shifts at 0 and a few cents of "
      "detune, you get a thick, chorus-like doubling." },
    { "pitch", "Dry", "The volume of your original signal. Turn it to 0% to hear only the shifted voices." },
    { "pitch", "Window", WINDOW_HELP },
    { "pitch", "Tone",
      "Darkens the shifted voices, which smooths out any graininess and helps them sit behind your playing. All "
      "the way up it's off." },

    // Whammy
    { "whammy", "Position",
      "The pedal position, from no shift (0%) to the full Interval (100%). Map an expression pedal to it with MIDI "
      "learn and rock it for pitch bends and dive bombs." },
    { "whammy", "Interval",
      "How far the pitch bends at 100% Position, from two octaves up to two octaves down. Dive drops three octaves." },
    { "whammy", "Mode",
      "Whammy plays only the shifted sound, for bends. Harmony adds your dry signal, for a fixed harmony." },
    { "whammy", "Window", WINDOW_HELP },

    // Harmonizer
    { "harmonizer", "Key",
      "The key of the song. The harmonies stay in this key, so a 'third above' is major or minor as the key needs." },
    { "harmonizer", "Scale",
      "The scale of the song, together with Key: it decides which notes the harmonies use. Major and Minor "
      "cover most songs; the modes suit other styles." },
    { "harmonizer", "Voice 1",
      "How far voice 1 is from your note, in scale steps: +3rd is a third above, -4th a fourth below, and so on." },
    { "harmonizer", "Level 1",
      "The volume of voice 1. Set it a little below your dry signal so the harmony supports your playing "
      "instead of covering it. At 0% voice 1 is off." },
    { "harmonizer", "Voice 2", "How far voice 2 is from your note, in scale steps. Try +3rd and +5th for a triad." },
    { "harmonizer", "Level 2",
      "The volume of voice 2. At 0% voice 2 is off; raise it to add a second harmony and make a three-part "
      "chord with your playing." },
    { "harmonizer", "Dry",
      "The volume of your original signal. Keep it at 100% to play along with the harmonies; turn it down to "
      "hear only the harmony voices." },
    { "harmonizer", "Window", WINDOW_HELP },

    // Synth
    { "synth", "Wave",
      "The synth's basic sound. Square is hollow and woody, Saw is bright and buzzy, Pulse is nasal, and Sub adds a "
      "square wave an octave down for weight. Play single notes." },
    { "synth", "Octave",
      "Moves the synth up or down in octaves from the note you play. -1 or -2 give a synth bass; +1 a bright "
      "lead." },
    { "synth", "Cutoff",
      "How bright the synth's filter is before your playing opens it. Low settings are dark and muffled; high "
      "settings bright." },
    { "synth", "Resonance", "Adds a squelchy, whistling peak to the filter. High values give an acid-synth sound." },
    { "synth", "Env",
      "How far your playing opens the filter. Pick harder and the sound gets brighter, for a funky, quacky sound." },
    { "synth", "Attack",
      "How long each synth note takes to fade in. Short attacks follow your picking closely; long attacks give "
      "a soft, swelling pad." },
    { "synth", "Glide",
      "How long the synth slides from one note to the next. Short glide follows your playing tightly; longer "
      "glide gives a smooth portamento, like a vintage mono synth lead." },
    { "synth", "Mix", MIX_HELP("synth") },

    // Filter
    { "filter", "Type",
      "The kind of filter. Low-pass keeps the lows (a 'wah' sweep), Band-pass keeps a narrow band (vocal), "
      "High-pass keeps the highs (thin) and Notch removes a narrow band (phaser-like)." },
    { "filter", "Cutoff",
      "Where the filter sits: the middle of the sweep. Low settings sound dark and muffled, high settings "
      "bright and thin. The LFO moves the filter up and down around this point." },
    { "filter", "Resonance", "Adds a ringing peak at the cutoff. High values make the sweep more vocal and squelchy." },
    { "filter", "Shape",
      "How the filter moves: smooth Sine or Triangle waves, ramping Saw Up or Saw Down, jumping Square, or random "
      "Steps for a sample-and-hold sound." },
    { "filter", "Rate",
      "How fast the filter sweeps. Slow rates give a slow, evolving movement; fast rates a rhythmic, pulsing "
      "one. With Sync on, it picks a note value instead." },
    { "filter", "Sync", SYNC_HELP("rate") },
    { "filter", "Depth",
      "How far the filter sweeps up and down around the Cutoff. 0% holds it still, as a fixed filter; 100% "
      "sweeps three octaves each way, for a dramatic, rhythmic effect." },
    { "filter", "Mix", MIX_HELP("filtered sound") },

    // Wah
    { "wah", "Position",
      "The pedal position, from heel (0%, dark) to toe (100%, bright). Map an expression pedal to it with MIDI learn "
      "and rock it as you play. Parked halfway, it's a 'cocked wah' tone." },
    { "wah", "Type",
      "The voicing. Classic is the famous Cry Baby. Vox is smoother and vocal. Wide sweeps further. Bass is tuned "
      "lower for bass guitar." },
    { "wah", "Resonance",
      "How sharp and vocal the wah's peak is. 50% is the type's own setting; higher values give a more "
      "pronounced, quacky wah, lower values a smoother one." },
    { "wah", "Level",
      "The output volume of the block. A wah can change the level as it sweeps; use this to match the volume "
      "with bypass." },

    // Auto Wah
    { "autowah", "Sensitivity",
      "How strongly your playing moves the filter. Raise it until your picking opens the wah the way you want." },
    { "autowah", "Decay",
      "How quickly the filter falls back after each note. Short decays give a snappy, percussive quack on every "
      "pick; long decays make the filter glide down slowly, for a smoother, more vocal sweep." },
    { "autowah", "Low",
      "Where the filter sits when you play softly (or don't play). Low settings start dark and muffled, higher "
      "settings start brighter. The sweep moves between Low and High." },
    { "autowah", "High",
      "How far up the filter sweeps when you play hard. Higher settings give a brighter, more dramatic sweep; "
      "lower settings keep it subtle and warm. Keep it above Low." },
    { "autowah", "Resonance",
      "How sharp and vocal the sweep sounds. Low values are smooth and subtle; high values give a squelchy, "
      "quacky funk sound that really talks." },
    { "autowah", "Direction",
      "Up opens the filter as you play harder, the classic envelope filter. Down closes it instead." },
    { "autowah", "Filter",
      "The filter's voicing: Wah like a wah pedal, Low-pass for a bubbly, synth-like sound, Band-pass for a vocal "
      "sound, or High-pass for a thin, quirky one." },
    { "autowah", "Mix", MIX_HELP("filtered sound") },
};

#undef MIX_HELP
#undef SYNC_HELP
#undef WINDOW_HELP
#undef WIDTH_HELP

struct GlobalHelp {
    const char* symbol;
    const char* help;
};

constexpr GlobalHelp kGlobalHelp[] = {
    { "input_mode",
      "Mono processes one channel; Stereo processes both channels, so stereo effects (wide delays, reverbs, "
      "choruses) can spread across the speakers. Stereo uses about twice the CPU." },
    { "input",
      "The level of your guitar going into the chain. Turn it up if your guitar is quiet, so amps and drives react "
      "as they should; turn it down if the IN meter clips." },
    { "bass", "A global EQ at the end of the chain: boosts or cuts the lows, for weight and warmth." },
    { "middle", "A global EQ at the end of the chain: boosts or cuts the mids, the body and punch of the guitar." },
    { "treble", "A global EQ at the end of the chain: boosts or cuts the highs, for brightness and clarity." },
    { "output",
      "The final volume of the plugin. Use it to set the level for your DAW without changing the sound." },
    { "bypass",
      "Turns the whole plugin off: your guitar passes through unprocessed, and the tuner and metronome go quiet." },
    { "limiter",
      "A safety limiter at the very end that stops the output from going over 0 dB, so it never clips. Keep it on "
      "unless you want to hear the raw level." },
    { "merge_balance",
      "The balance between the two branches of the split. In the middle both play at full level; turn left to fade "
      "out branch B, right to fade out branch A." },
    { "merge_level_a",
      "The volume of branch A (top row) where the branches merge. Use it to balance the two branches, for "
      "example a dry amp against a wet effects branch." },
    { "merge_level_b",
      "The volume of branch B (bottom row) where the branches merge. Use it to balance the two branches, for "
      "example a dry amp against a wet effects branch." },
    { "merge_pan_a",
      "Pans branch A (top row) left or right, for example to put two amps on opposite sides for a wide stereo "
      "sound. Stereo mode only." },
    { "merge_pan_b",
      "Pans branch B (bottom row) left or right, for example to put two amps on opposite sides for a wide "
      "stereo sound. Stereo mode only." },
    { "merge_invert_a",
      "Flips branch A's phase. If the merged sound gets thin or hollow, the branches are cancelling each other; try "
      "flipping one." },
    { "merge_invert_b",
      "Flips branch B's phase. If the merged sound gets thin or hollow, the branches are cancelling each other; try "
      "flipping one." },
    { "tuner",
      "Opens the tuner in place of the chain. Play a single open string and tune until the display shows you're "
      "in tune. While it's open your guitar is muted, unless Mute is off." },
    { "tuner_mute",
      "Mutes your guitar while the tuner is open, so you can tune silently on stage or in a session. Turn it "
      "off to hear yourself while tuning." },
    { "tuner_ref",
      "The pitch of A4 that the tuner tunes to. 440 Hz is standard; change it only to play along with something "
      "tuned differently." },
    { "tuner_strobe",
      "Switches between the needle view and the strobe view. The strobe bands drift left when you're flat and "
      "right when you're sharp, and stand still when you're in tune; it's the most precise view." },
    { "metro", "Starts and stops the metronome. With DAW sync on, it clicks while the DAW plays." },
    { "metro_tempo",
      "The metronome's speed in beats per minute. Drag the slider, use the -/+ buttons, the arrow keys, or tap "
      "the tempo with the Tap button." },
    { "metro_volume",
      "The volume of the click. The click is added after the Output knob and before the limiter, so it can't "
      "clip the output." },
    { "metro_sync",
      "Follows your DAW's tempo, time signature and transport instead of the metronome's own settings." },
    { "metro_beats",
      "How many beats there are in a bar. Set it to the top number of the time signature: 4 for 4/4, 3 for 3/4, "
      "7 for 7/8." },
    { "metro_note",
      "Which note value gets the beat, the bottom number of the time signature: 4 for quarter notes (4/4, 3/4), "
      "8 for eighths (6/8, 7/8), and so on." },
    { "metro_grouping",
      "How the beats are grouped, for odd and compound meters. For example, 7/8 can be 2+2+3 or 3+2+2. Each group "
      "starts with an accent." },
    { "metro_sub",
      "Adds clicks between the beats: eighths, triplets, sixteenths, quintuplets, sextuplets, swing, dotted and "
      "mixed figures. Great for practicing rhythmic precision." },
    { "metro_sound",
      "The click's sound: Classic is a woodblock-like click, Digital a short electronic tick and Beep a pure "
      "tone. Pick whatever cuts through your playing best." },
    { "metro_sub_volume",
      "The volume of the subdivision clicks, relative to the main volume. Keep it lower than the beats so the "
      "beat stays clear." },
    { "metro_trainer",
      "Changes the tempo for you as you practice. Step raises (or lowers) it by a set amount every few bars; "
      "Gradual ramps it smoothly from the start to the end tempo." },
    { "metro_tr_start",
      "The tempo the trainer starts at when you start the metronome. Start at a tempo you can play cleanly." },
    { "metro_tr_end",
      "The tempo the trainer ends at. It can be slower than the start tempo, to practice slowing down. The "
      "trainer stays at this tempo once it gets there." },
    { "metro_tr_step",
      "How many BPM the tempo changes by on each step. Small steps (2-5 BPM) build speed gradually; bigger "
      "steps push you harder." },
    { "metro_tr_every",
      "How many bars the trainer plays at each tempo before it steps to the next. More bars give you more time "
      "to settle in." },
    { "metro_tr_bars",
      "How many bars the gradual ramp takes to go from the start tempo to the end tempo. More bars make the "
      "change slower and easier to follow." },
    { "metro_gap",
      "Mutes the click for some bars, then brings it back, so you can check that you keep time on your own." },
    { "metro_gap_play",
      "How many bars the click plays during the gap trainer, before it mutes. Use a few bars to lock in the "
      "tempo before the gap." },
    { "metro_gap_mute",
      "How many bars the click stays muted during the gap trainer. Longer gaps test your timing harder. Keep "
      "playing in time; the click comes back where it should be." },
};

constexpr const char* kAccentHelp =
    "Click a beat to cycle it through accent (loudest), normal, soft and mute, to make the click's pattern.";

} // namespace

const char* blockParamHelp(const char* typeId, const char* paramName)
{
    if (typeId == nullptr || paramName == nullptr)
        return nullptr;
    for (const BlockHelp& entry : kBlockHelp)
    {
        if (std::strcmp(entry.typeId, typeId) == 0 && std::strcmp(entry.param, paramName) == 0)
            return entry.help;
    }
    return nullptr;
}

const char* globalParamHelp(uint32_t index)
{
    if (isSlotParam(index) || index >= kParamCount)
        return nullptr;
    if (index >= kParamMetroAccent1 && index <= kParamMetroAccent16)
        return kAccentHelp;
    const char* symbol = globalParamSpec(index).symbol;
    for (const GlobalHelp& entry : kGlobalHelp)
    {
        if (std::strcmp(entry.symbol, symbol) == 0)
            return entry.help;
    }
    return nullptr;
}

} // namespace namsayin
