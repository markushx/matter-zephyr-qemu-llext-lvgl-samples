#include "BulbUi.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>

#include <lvgl.h>

namespace {

struct BulbState
{
    bool onOff;
    uint8_t level;
};

K_MSGQ_DEFINE(sStateQueue, sizeof(BulbState), 4, 4);

lv_obj_t * sArc;
lv_obj_t * sLabel;

void ApplyState(const BulbState & state)
{
    // Matter's level range is 1-254; show it as a 0-100% arc.
    uint32_t percent = state.onOff ? (static_cast<uint32_t>(state.level) * 100) / 254 : 0;

    lv_arc_set_value(sArc, static_cast<int32_t>(percent));
    lv_obj_set_style_arc_color(sArc, state.onOff ? lv_palette_main(LV_PALETTE_YELLOW) : lv_palette_main(LV_PALETTE_GREY),
                               LV_PART_INDICATOR);
    lv_label_set_text_fmt(sLabel, state.onOff ? "ON\n%u%%" : "OFF", static_cast<unsigned>(percent));
}

void GuiThread(void *, void *, void *)
{
    const struct device * display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display))
    {
        return;
    }

    sArc = lv_arc_create(lv_screen_active());
    lv_obj_set_size(sArc, 150, 150);
    lv_obj_center(sArc);
    lv_arc_set_rotation(sArc, 270);
    lv_arc_set_bg_angles(sArc, 0, 360);
    lv_arc_set_range(sArc, 0, 100);
    lv_obj_remove_style(sArc, nullptr, LV_PART_KNOB | LV_STATE_ANY);
    lv_obj_clear_flag(sArc, LV_OBJ_FLAG_CLICKABLE);

    sLabel = lv_label_create(lv_screen_active());
    lv_obj_center(sLabel);

    ApplyState(BulbState{ false, 0 });
    lv_timer_handler();
    display_blanking_off(display);

    while (true)
    {
        BulbState state;
        while (k_msgq_get(&sStateQueue, &state, K_NO_WAIT) == 0)
        {
            ApplyState(state);
        }
        lv_timer_handler();
        k_msleep(10);
    }
}

// Statically-reserved stack, but NOT auto-started via K_THREAD_DEFINE:
// K_THREAD_DEFINE'd threads start at raw boot time, before CHIP's own stack
// is up. Started explicitly instead, from AppTask::Init(), once CHIP's
// stack (and ideally the whole server) is already initialized -- there's no
// reason for a UI thread to run any earlier than that, and it avoids
// competing with early boot/init work for no benefit. (The actual root
// cause of this port's networking breakage under CONFIG_DISPLAY=y turned
// out to be an e1000 PCI slot/IRQ collision, not thread timing -- see
// scripts/run-qemu-macos.sh -- but starting this thread late is still the
// right call on its own merits.)
K_THREAD_STACK_DEFINE(sGuiThreadStack, 8192);
struct k_thread sGuiThreadData;

} // namespace

void BulbUi::Init()
{
    k_thread_create(&sGuiThreadData, sGuiThreadStack, K_THREAD_STACK_SIZEOF(sGuiThreadStack), GuiThread, nullptr, nullptr,
                    nullptr, K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
}

void BulbUi::SetState(bool onOff, uint8_t level)
{
    BulbState state{ onOff, level };
    k_msgq_put(&sStateQueue, &state, K_NO_WAIT);
}
