#include "test_main.h"

#include "../proxy/combo.h"

using namespace rezgaze;

TEST(combo_needs_the_full_hold) {
    ComboDetector c(ComboDetector::Both, 2.0);
    EXPECT(!c.Update(true, true, 0.0));
    EXPECT(!c.Update(true, true, 1.9));
    EXPECT(c.Update(true, true, 2.0));
}

TEST(combo_fires_once_until_released) {
    ComboDetector c(ComboDetector::Both, 2.0);
    c.Update(true, true, 0.0);
    EXPECT(c.Update(true, true, 2.1));
    EXPECT(!c.Update(true, true, 3.0));
    EXPECT(!c.Update(true, true, 9.0));
    c.Update(false, false, 9.1);
    c.Update(true, true, 10.0);
    EXPECT(c.Update(true, true, 12.0));
}

TEST(combo_both_needs_both_sticks) {
    ComboDetector c(ComboDetector::Both, 2.0);
    c.Update(true, false, 0.0);
    EXPECT(!c.Update(true, false, 5.0));
    // The hold starts when the second stick goes down.
    c.Update(true, true, 5.0);
    EXPECT(!c.Update(true, true, 6.5));
    EXPECT(c.Update(true, true, 7.0));
}

TEST(combo_letting_go_restarts_the_hold) {
    ComboDetector c(ComboDetector::Both, 2.0);
    c.Update(true, true, 0.0);
    c.Update(true, false, 1.5);
    c.Update(true, true, 1.6);
    EXPECT(!c.Update(true, true, 3.0));
    EXPECT(c.Update(true, true, 3.6));
}

TEST(combo_one_accepts_either_stick) {
    ComboDetector c(ComboDetector::One, 3.0);
    c.Update(false, true, 0.0);
    EXPECT(!c.Update(false, true, 2.9));
    EXPECT(c.Update(false, true, 3.0));
}

TEST(combo_masks_the_sticks_after_firing_until_release) {
    ComboDetector c(ComboDetector::Both, 2.0);
    c.Update(true, true, 0.0);
    EXPECT(!c.Masking());  // before it fires, the game sees the presses
    c.Update(true, true, 2.0);
    EXPECT(c.Masking());
    c.Update(true, false, 2.5);
    EXPECT(c.Masking());  // still held on one side
    c.Update(false, false, 2.6);
    EXPECT(!c.Masking());
}
