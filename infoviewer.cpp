#include <SDL/SDL.h>
#include <SDL/SDL_gfxPrimitives.h>
#include <SDL/SDL_image.h>

typedef struct
{
	SDL_Surface *screen;
	int scr_w, scr_h;
	int xsteps, ysteps;
} screen_descriptor_t;

void draw_box(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool border, const int r, const int g, const int b)
{
	int x1 = x * sd->xsteps;
	int y1 = y * sd->ysteps;
	int x2 = x1 + sd->xsteps * w - 1;
	int y2 = y1 + sd->ysteps * h - 1;

	boxRGBA(sd->screen, x1, y1, x2, y2, r, g, b, 255);
	rectangleRGBA(sd->screen, x1, y1, x2, y2, r, g, b, 191);
}

int main(int argc, char *argv[])
{
	SDL_Init(SDL_INIT_VIDEO);
	atexit(SDL_Quit);

	int n_columns = 80, n_rows = 25;
	bool grid = true;

	const SDL_VideoInfo *svi = SDL_GetVideoInfo();
	SDL_Surface *screen = SDL_SetVideoMode(0, 0, 32, (svi->hw_available ? SDL_HWSURFACE : SDL_SWSURFACE) | SDL_ASYNCBLIT);
	const int w = screen->w;
	const int h = screen->h;
	const int xsteps = w / n_columns;
	const int ysteps = h / n_rows;
	screen_descriptor_t sd { screen, w, h, xsteps, ysteps };

	if (grid) {
		for(int cy=0; cy<n_rows; cy++)
			lineRGBA(screen, 0, cy * ysteps, w, cy * ysteps, 255, 255, 255, 255);

		for(int cx=0; cx<n_columns; cx++)
			lineRGBA(screen, cx * xsteps, 0, cx * xsteps, h, 255, 255, 255, 255);
	}

	draw_box(&sd, 0, 0, 16, 8, true, 80, 255, 80);

	SDL_UpdateRect(screen, 0, 0, w, h);

	getchar();

	return 0;
}
