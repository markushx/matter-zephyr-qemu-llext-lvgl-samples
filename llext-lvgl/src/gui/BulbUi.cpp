#include "BulbUi.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <lvgl.h>

namespace {

struct BulbState
{
    bool onOff;
    uint8_t level;
};

K_MSGQ_DEFINE(sStateQueue, sizeof(BulbState), 4, 4);

// The built-in UI, shown until an extension provides another one.
lv_obj_t * sArc;
lv_obj_t * sLabel;

void BuiltinCreate(lv_obj_t * parent)
{
    sArc = lv_arc_create(parent);
    lv_obj_set_size(sArc, 150, 150);
    lv_obj_center(sArc);
    lv_arc_set_rotation(sArc, 270);
    lv_arc_set_bg_angles(sArc, 0, 360);
    lv_arc_set_range(sArc, 0, 100);
    lv_obj_remove_style(sArc, nullptr, LV_PART_KNOB | LV_STATE_ANY);
    lv_obj_clear_flag(sArc, LV_OBJ_FLAG_CLICKABLE);

    sLabel = lv_label_create(parent);
    lv_obj_center(sLabel);
}

void BuiltinUpdate(bool on, uint8_t percent)
{
    lv_arc_set_value(sArc, on ? percent : 0);
    lv_obj_set_style_arc_color(sArc, on ? lv_palette_main(LV_PALETTE_YELLOW) : lv_palette_main(LV_PALETTE_GREY),
                               LV_PART_INDICATOR);
    lv_label_set_text_fmt(sLabel, on ? "ON\n%u%%" : "OFF", static_cast<unsigned>(percent));
}

constexpr BulbUi::Ui kBuiltinUi = { BuiltinCreate, BuiltinUpdate };

// Everything below up to the request variables is only touched on the GUI
// thread.
BulbUi::Ui sUi;
lv_obj_t * sRoot;
BulbState sState = { false, 0 };

void ApplyState()
{
    // Matter's level range is 1-254; UIs get it as 0-100%.
    sUi.update(sState.onOff, static_cast<uint8_t>((static_cast<uint32_t>(sState.level) * 100) / 254));
}

void ShowUi(const BulbUi::Ui & ui)
{
    // Each UI gets its own root container, so deleting that removes
    // everything the previous UI created -- including any styling it applied
    // to its parent -- without it having to clean up after itself.
    if (sRoot != nullptr)
    {
        lv_obj_delete(sRoot);
    }
    sRoot = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(sRoot);
    lv_obj_set_size(sRoot, lv_pct(100), lv_pct(100));

    sUi = ui;
    sUi.create(sRoot);
    ApplyState();
}

// A UI switch requested by SetUi(), handed over to the GUI thread.
K_MUTEX_DEFINE(sRequestLock);
K_SEM_DEFINE(sUiSwitched, 0, 1);
BulbUi::Ui sRequestedUi;
bool sUiRequested;
atomic_t sRunning;

void GuiThread(void *, void *, void *)
{
    const struct device * display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display))
    {
        return;
    }

    ShowUi(kBuiltinUi);
    lv_timer_handler();
    display_blanking_off(display);
    atomic_set(&sRunning, 1);

    while (true)
    {
        k_mutex_lock(&sRequestLock, K_FOREVER);
        bool switchUi          = sUiRequested;
        BulbUi::Ui requestedUi = sRequestedUi;
        sUiRequested           = false;
        k_mutex_unlock(&sRequestLock);
        if (switchUi)
        {
            ShowUi(requestedUi);
            k_sem_give(&sUiSwitched);
        }

        BulbState state;
        bool changed = false;
        while (k_msgq_get(&sStateQueue, &state, K_NO_WAIT) == 0)
        {
            sState  = state;
            changed = true;
        }
        if (changed)
        {
            ApplyState();
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

bool BulbUi::SetUi(const Ui * ui)
{
    if (!atomic_get(&sRunning))
    {
        return false;
    }

    k_mutex_lock(&sRequestLock, K_FOREVER);
    sRequestedUi = ui != nullptr ? *ui : kBuiltinUi;
    sUiRequested = true;
    k_sem_reset(&sUiSwitched);
    k_mutex_unlock(&sRequestLock);

    // No timeout: returning early would let the caller unload an extension
    // the GUI thread is still using.
    k_sem_take(&sUiSwitched, K_FOREVER);
    return true;
}
