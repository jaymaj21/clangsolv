#include "workflow.hpp"
int main() {
    const auto saved=example::persist(example::Article{"Too early"});
    example::publish(saved); // Missing Validated and Authenticated.
}
