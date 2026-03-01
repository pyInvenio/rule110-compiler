#include "Verifier.hpp"
#include "Decoder.hpp"

int main() {
    int failures = 0;
    failures += Rule110::Verifier::run_all_tm_tests(true);
    failures += Rule110::Verifier::run_all_tag_tests(true);
    failures += Rule110::Verifier::run_all_cts_tests(true);
    failures += Rule110::Verifier::run_all_r110_tests(true);
    failures += Rule110::Decoder::run_all_decode_tests(true);
    return failures > 0 ? 1 : 0;
}
