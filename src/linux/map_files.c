#include "error.h"
#include "display.h"
#include "gui/image.h"

#include "gui.h"

#include "icons_32/icons_32.h"

uint8_t sd_status = PM_OK;

error_code_t statusRender(const display_t* dsp, void* comp)
{
    image_t* icon = sd_indicator_label->child;
    if (sd_status == PM_OK)
        icon->data = SD; // show SD card symbol
    else
        icon->data = noSD; // show SD card symbol

    return PM_OK;
}
