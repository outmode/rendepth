#pragma once
#include <SDL3/SDL_dialog.h>
namespace MediaOpenDialog {
void open(SDL_DialogFileCallback callback, SDL_Window* parent,
          const SDL_DialogFileFilter* filters, int filterCount);
void poll();
void close();
}
