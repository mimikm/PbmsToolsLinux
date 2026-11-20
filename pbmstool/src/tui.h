#pragma once
#include "bms.h"
#include "params.h"
#include <ncurses.h>
#include <chrono>
#include <string>
#include <vector>

namespace pace {

// A single editable row in the params tab.
struct ParamRow {
    enum class Kind { BOOL, DOUBLE, INT, HEADING };
    Kind        kind;
    std::string label;
    std::string unit;
    // Pointers into AllParams current_ fields (null for HEADING rows)
    bool*   bval = nullptr;
    double* dval = nullptr;
    int*    ival = nullptr;
    int     group_id = -1;  // maps to DirtyFlags field; -1 = not editable
    int  precision = 3;  // decimal places for display of doubles
};

class TUI {
public:
    TUI(BMS& bms, const std::string& port);
    ~TUI();
    void run();

private:
    BMS&        bms_;
    std::string port_;
    int         pack_count_   = 1;
    int         current_pack_ = 1;
    int         current_tab_  = 0;   // 0=Analog 1=Alarms 2=Params
    bool        auto_refresh_ = true;

    AnalogData  analog_;
    AlarmData   alarm_;
    AllParams   params_;
    AllParams   params_orig_;
    DirtyFlags  dirty_;

    // Params tab cursor & editing
    int         param_cursor_  = 0;
    int         param_scroll_  = 0;
    bool        edit_mode_     = false;
    std::string edit_buf_;
    std::vector<ParamRow> param_rows_;

    // Footer message and last-refresh timestamp
    std::string footer_msg_;
    std::chrono::steady_clock::time_point last_refresh_;

    // Set true after a pack switch until live data / params arrive successfully
    bool data_stale_   = true;
    bool params_stale_ = true;

    void init_ncurses();
    void draw_all();
    void draw_header();
    void draw_tabs();
    void draw_content();
    void draw_analog();
    void draw_alarms();
    void draw_params();
    void draw_footer();

    void refresh_live_data();
    bool load_params(std::string& err);
    bool save_dirty_params(std::string& err);
    void revert_params();
    void switch_pack(int delta);
    void handle_params_key(int ch);
    void build_param_rows();

    int  content_rows() const;
    int  content_cols() const;
    void mark_dirty(int group_id);

    static int cell_color_pair(uint8_t volt_alarm, bool balancing, attr_t& extra_attr);
};

} // namespace pace
