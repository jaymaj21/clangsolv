#include "workflow.hpp"
int main() {
    const std::string credential="demo-author";
    auto saved=example::persist(example::Article{"Before editing"});
    auto checked=example::validate(saved);
    auto ready=example::authenticate(checked,credential);
    ready.title="Changed after checking";
    example::publish(ready); // Mutation discarded the old evidence.
}
