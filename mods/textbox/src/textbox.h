#ifndef TEXTBOX_H
#define TEXTBOX_H

#include "hoshi/settings.h"

#include "textbox_api.h"

extern OptionDesc TextBox_ModSettings;

void TextBox_OnSceneChange(void);

int TextBox_Enqueue(const char *format, ...);
int TextBox_EnqueueSegments(const TextSegment *segs, int seg_count);
int TextBox_EnqueueColoredNoun(const char *prefix, const char *noun, GXColor noun_color, const char *suffix);
int TextBox_EnqueueColoredNounFmt(const char *prefix, const char *noun, GXColor noun_color,
                                  const char *suffix_format, ...);

#endif // TEXTBOX_H
