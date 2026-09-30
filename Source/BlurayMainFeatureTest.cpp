#include "BlurayMainFeature.h"
#include <cassert>

int main() {
    BlurayTitle commentary;
    commentary.index = 0;
    commentary.duration = 7200;
    commentary.chapterCount = 20;
    commentary.audioLanguages = {"jpn"};
    commentary.audioStreamLanguages = {{100, "jpn"}, {101, "jpn"}};
    commentary.subtitleStreamLanguages = {{200, "jpn"}, {201, "jpn"}};
    commentary.mainFeatureCandidate = true;

    BlurayTitle movie = commentary;
    movie.index = 1;
    movie.mainFeatureCandidate = false;
    movie.audioLanguages = {"jpn", "eng", "fra", "deu"};
    movie.subtitleStreamLanguages = {{200, "jpn"}, {201, "eng"}};
    assert(BlurayMainFeature::languageCount(commentary) == 1);
    assert(BlurayMainFeature::languageCount(movie) == 4);
    assert(BlurayMainFeature::select({commentary, movie}) == movie.index);

    movie.audioLanguages = commentary.audioLanguages;
    assert(BlurayMainFeature::select({commentary, movie}) == movie.index);
    movie.audioLanguages = {"jpn", "eng", "fra", "deu"};

    // Language-rich extras and substantially shorter cuts cannot take over.
    movie.duration = commentary.duration - 2;
    assert(BlurayMainFeature::select({commentary, movie}) == commentary.index);

    // Without a language difference, chapters and then the disc hint break ties.
    movie.duration = commentary.duration;
    movie.audioLanguages = commentary.audioLanguages;
    movie.subtitleStreamLanguages = commentary.subtitleStreamLanguages;
    movie.chapterCount = commentary.chapterCount + 1;
    assert(BlurayMainFeature::select({commentary, movie}) == movie.index);
    movie.chapterCount = commentary.chapterCount;
    assert(BlurayMainFeature::select({commentary, movie}) == commentary.index);
}
