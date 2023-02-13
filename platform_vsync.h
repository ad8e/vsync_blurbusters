#pragma once

//these are signed because you do math with them. (add, subtract, divide)
extern int total_scanlines; //such as 1125
extern int active_scanlines; //such as 1080
extern int porch_scanlines; //porch = 1125 - 1080
extern int scanlines_between_sync_and_first_displayed_line; //VBI + back porch. it's at least 1.
