#include "internal.hpp"
#include "msakit/core.hpp"

int main(int argc, char** argv) {
    return msakit::detail::run_application(argc, argv, msakit::align);
}
