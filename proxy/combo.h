// A held-button combination: fires once when the thumbstick(s) have been held down long enough,
// then not again until released. After it fires the sticks are masked from the game until let go,
// so the hold that triggered it doesn't also reach Rez.
#pragma once

namespace rezgaze {

class ComboDetector {
public:
    enum Need { Both, One };
    ComboDetector(Need need, double hold_s) : need_(need), hold_s_(hold_s) {}
    void Set(Need need, double hold_s) {
        need_ = need;
        hold_s_ = hold_s;
    }

    // Current stick states and time (seconds); true on the update the hold completes.
    bool Update(bool left, bool right, double t) {
        bool any = left || right;
        bool down = need_ == Both ? (left && right) : any;
        if (!any) masking_ = false;
        if (!down) {
            holding_ = false;
            fired_ = false;
            return false;
        }
        if (!holding_) {
            holding_ = true;
            since_ = t;
        }
        if (!fired_ && t - since_ >= hold_s_) {
            fired_ = true;
            masking_ = true;
            return true;
        }
        return false;
    }

    bool Masking() const { return masking_; }

private:
    Need need_;
    double hold_s_;
    bool holding_ = false, fired_ = false, masking_ = false;
    double since_ = 0;
};

}  // namespace rezgaze
