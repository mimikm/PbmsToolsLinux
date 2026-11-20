#pragma once
#include "bms.h"
#include <cstdint>
#include <string>

namespace pace {

// Aggregate of every param struct — held as current (editable) + original (for revert).
struct AllParams {
    CellVoltProtParams cell_ovp;
    CellVoltProtParams pack_ovp;
    CurrProtParams     chg_ocp;
    CurrProtParams     dchg_ocp;
    ShortCircuitParams scp;
    CellTempProtParams chg_otp;
    MosfetOtpParams    mosfet_otp;
    UnderTempParams    utp;
    BalanceParams      balance;
    PackParamsBParams  pack_b;
    EqualizationParams equalization;
    TempProtGroup      chg_temp;
    TempProtGroup      dchg_temp;
    TempProtGroup      mos_chg_temp;
    TempProtGroup      mos_dchg_temp;
};

// Group IDs used by the TUI and GUI to track which groups were edited.
enum ParamGroup : int {
    PG_CELL_OVP = 0, PG_PACK_OVP, PG_CHG_OCP, PG_DCHG_OCP,
    PG_SCP, PG_CHG_OTP, PG_MOSFET_OTP, PG_UTP,
    PG_BALANCE, PG_PACK_B, PG_EQUALIZATION,
    PG_CHG_TEMP, PG_DCHG_TEMP, PG_MOS_CHG_TEMP, PG_MOS_DCHG_TEMP,
    PG_COUNT
};

// Dirty flags — one per write-able group.
struct DirtyFlags {
    bool cell_ovp=false, pack_ovp=false;
    bool chg_ocp=false, dchg_ocp=false;
    bool scp=false, chg_otp=false;
    bool mosfet_otp=false, utp=false;
    bool balance=false, pack_b=false;
    bool equalization=false;
    bool chg_temp=false, dchg_temp=false;
    bool mos_chg_temp=false, mos_dchg_temp=false;
    bool any() const {
        return cell_ovp||pack_ovp||chg_ocp||dchg_ocp||scp||chg_otp||
               mosfet_otp||utp||balance||pack_b||equalization||
               chg_temp||dchg_temp||mos_chg_temp||mos_dchg_temp;
    }
    void clear() { *this = DirtyFlags{}; }

    bool* by_id(int id) {
        switch (id) {
            case PG_CELL_OVP:      return &cell_ovp;
            case PG_PACK_OVP:      return &pack_ovp;
            case PG_CHG_OCP:       return &chg_ocp;
            case PG_DCHG_OCP:      return &dchg_ocp;
            case PG_SCP:           return &scp;
            case PG_CHG_OTP:       return &chg_otp;
            case PG_MOSFET_OTP:    return &mosfet_otp;
            case PG_UTP:           return &utp;
            case PG_BALANCE:       return &balance;
            case PG_PACK_B:        return &pack_b;
            case PG_EQUALIZATION:  return &equalization;
            case PG_CHG_TEMP:      return &chg_temp;
            case PG_DCHG_TEMP:     return &dchg_temp;
            case PG_MOS_CHG_TEMP:  return &mos_chg_temp;
            case PG_MOS_DCHG_TEMP: return &mos_dchg_temp;
            default:               return nullptr;
        }
    }
    void set(int id) { if (bool* f = by_id(id)) *f = true; }
    bool get(int id) const {
        bool* f = const_cast<DirtyFlags*>(this)->by_id(id);
        return f && *f;
    }
};

// Read every parameter group for the given pack address.
// The optional MOS temperature groups never cause failure (present=false instead).
bool read_all_params(BMS& bms, uint8_t addr, AllParams& out, std::string& err);

// Write only the groups flagged in `dirty`. Stops at the first failure.
bool write_dirty_params(BMS& bms, const AllParams& p, const DirtyFlags& dirty,
                        std::string& err);

} // namespace pace
