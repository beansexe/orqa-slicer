#pragma once
namespace Slic3r { namespace GUI {
class Plater;
bool multi_print_enabled();
void set_multi_print_enabled(Plater* plater, bool enabled);
void print_to_all_printers();
}}
