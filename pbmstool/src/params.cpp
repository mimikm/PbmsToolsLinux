#include "params.h"

namespace pace {

bool read_all_params(BMS& bms, uint8_t addr, AllParams& p, std::string& err) {
    if (!bms.get_cell_ovp(addr, p.cell_ovp, err)) return false;
    if (!bms.get_pack_ovp(addr, p.pack_ovp, err)) return false;
    if (!bms.get_chg_ocp(addr,  p.chg_ocp,  err)) return false;
    if (!bms.get_dchg_ocp(addr, p.dchg_ocp, err)) return false;
    if (!bms.get_scp(p.scp, err))                 return false;
    if (!bms.get_cell_chg_otp(p.chg_otp, err))    return false;
    if (!bms.get_mosfet_otp(p.mosfet_otp, err))   return false;
    if (!bms.get_utp(p.utp, err))                 return false;
    if (!bms.get_balance(p.balance, err))         return false;
    if (!bms.get_pack_b(p.pack_b, err))           return false;
    if (!bms.get_equalization(p.equalization, err)) return false;
    if (!bms.get_chg_temp_prot(p.chg_temp, err))  return false;
    if (!bms.get_dchg_temp_prot(p.dchg_temp, err)) return false;

    // Optional groups — a failure here just means "not supported".
    std::string ignored;
    if (!bms.get_mos_chg_temp(p.mos_chg_temp, ignored))   p.mos_chg_temp.present = false;
    if (!bms.get_mos_dchg_temp(p.mos_dchg_temp, ignored)) p.mos_dchg_temp.present = false;
    return true;
}

bool write_dirty_params(BMS& bms, const AllParams& p, const DirtyFlags& d,
                        std::string& err) {
    if (d.cell_ovp     && !bms.set_cell_ovp(p.cell_ovp, err))           return false;
    if (d.pack_ovp     && !bms.set_pack_ovp(p.pack_ovp, err))           return false;
    if (d.chg_ocp      && !bms.set_chg_ocp(p.chg_ocp, err))             return false;
    if (d.dchg_ocp     && !bms.set_dchg_ocp(p.dchg_ocp, err))           return false;
    if (d.scp          && !bms.set_scp(p.scp, err))                     return false;
    if (d.chg_otp      && !bms.set_cell_chg_otp(p.chg_otp, err))        return false;
    if (d.mosfet_otp   && !bms.set_mosfet_otp(p.mosfet_otp, err))       return false;
    if (d.utp          && !bms.set_utp(p.utp, err))                     return false;
    if (d.balance      && !bms.set_balance(p.balance, err))             return false;
    if (d.pack_b       && !bms.set_pack_b(p.pack_b, err))               return false;
    if (d.equalization && !bms.set_equalization(p.equalization, err))   return false;
    if (d.chg_temp     && !bms.set_chg_temp_prot(p.chg_temp, err))      return false;
    if (d.dchg_temp    && !bms.set_dchg_temp_prot(p.dchg_temp, err))    return false;
    if (d.mos_chg_temp && p.mos_chg_temp.present &&
        !bms.set_mos_chg_temp(p.mos_chg_temp, err))                     return false;
    if (d.mos_dchg_temp && p.mos_dchg_temp.present &&
        !bms.set_mos_dchg_temp(p.mos_dchg_temp, err))                   return false;
    return true;
}

} // namespace pace
