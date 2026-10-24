#include "testing.h"
int main(int argc, char **argv)
{
    if (argc > 2 && std::string(argv[1]) == "--child") return octest::childMain(argv[2]);   // a CHILD case (testing.h)
    return octest::run(argc, argv);
}
