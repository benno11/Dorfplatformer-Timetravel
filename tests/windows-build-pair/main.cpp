extern "C" const unsigned char dfBuildPairSeal[256];
int main() { return dfBuildPairSeal[0] == 'D' ? 0 : 1; }
