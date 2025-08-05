#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

SDL_AppResult SDL_AppInit(void **, int, char *[]) {
	SDL_SetAppMetadata("Example Renderer Clear", "1.0",
			   "com.example.renderer-clear");

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	if (!SDL_CreateWindowAndRenderer("examples/renderer/clear", 640, 480, 0,
					 &window, &renderer)) {
		SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *, SDL_Event *event) {
	if (event->type == SDL_EVENT_QUIT) {
		return SDL_APP_SUCCESS;
	}
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *) {
	const double now = ((double) SDL_GetTicks()) / 1000.0;
	const float red = (float) (0.5 + 0.5 * SDL_sin(now));
	const float green =
		(float) (0.5 + 0.5 * SDL_sin(now + SDL_PI_D * 2 / 3));
	const float blue =
		(float) (0.5 + 0.5 * SDL_sin(now + SDL_PI_D * 4 / 3));
	SDL_SetRenderDrawColorFloat(renderer, red, green, blue,
				    SDL_ALPHA_OPAQUE_FLOAT);

	SDL_RenderClear(renderer);

	SDL_RenderPresent(renderer);

	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *, SDL_AppResult) {}
