#include "workflow.hpp"
int main(int argc,char**) {
    example::Article chosen;
    const auto saved=example::persist(example::Article{"A title"});
    const auto checked=example::validate(saved);
    if (argc>1) chosen=checked;
    else chosen=saved;
    clangsolv::require<example::tags::Validated>(chosen); // Not on every path.
}
