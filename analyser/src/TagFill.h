#pragma once
// Fill missing tags (artist, album, year, genre): from the folder names, then from MusicBrainz and Discogs. Only EMPTY fields are filled, into the
// user's own edits (never the audio files), every fill is logged so it can be undone.
#include <juce_core/juce_core.h>
#include <functional>
#include "Engine.h"

namespace mdark { namespace tagfill {

struct Facts { juce::String artist, album, year, genre; };
struct Found { juce::String year, genre; bool ok = false; };

juce::String genreFromFolderName(const juce::String& folderName);   // "Rock", "hip-hop", "Drum & Bass" -> a tidy genre name, else empty
Facts fromPath(const juce::String& fullPath);                       // the folder rules (genre folder above the file, Artist/Album layouts, (1997) in an album folder)
juce::String cleanTitle(const juce::String& title);                 // "Song (Remastered 2009)" / "Song - 2015 Remaster" / "Song (feat. X)" -> "Song"
Found parseMusicBrainz(const juce::String& json);                   // the first strong recording match: earliest release year, most-voted genre
Found parseDiscogs(const juce::String& json);                       // search results: earliest year, first genre
juce::String musicBrainzUrl(const juce::String& artist, const juce::String& title);
juce::String discogsUrl(const juce::String& artist, const juce::String& title, const juce::String& token);

struct Options { bool folders = true, musicBrainz = true, discogs = false; juce::String discogsToken; };
struct Summary { int years = 0, genres = 0, artists = 0, albums = 0, lookups = 0, failures = 0, tracks = 0; };

// Runs on the calling thread (call it from a background thread). progress(done, total, text); shouldStop() is polled.
Summary fill(Library& lib, const Options& o, std::function<void(int, int, const juce::String&)> progress, std::function<bool()> shouldStop);
int undoLast(Library& lib);   // removes the values the last runs wrote (only where they are still the same value); returns how many

}} // namespace
