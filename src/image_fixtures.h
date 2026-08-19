#ifndef ATM_IMAGE_FIXTURES_API_H
#define ATM_IMAGE_FIXTURES_API_H

/* Creates /home/exp-sample.png and /home/exp-sample.jpg if absent.
 * The assets are a native decoder regression corpus and a visible first-run
 * sample for Exp Image Viewer. */
void image_fixtures_seed(void);

#endif
