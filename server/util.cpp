#include "util.hpp"

bool getenv(const char* name, std::string& env)
{
    const char* ret = getenv(name);
    if (ret) {
        env = std::string(ret);
    }
    else {
        std::cout << "Env variable: " << name << " not set!!!";
    }
    return !!ret;
}

std::string decToHexa(size_t n)
{
    char hexaDeciNum[100];

    int i = 0;
    while (n != 0) {
        size_t temp = 0;

        temp = n % 16;

        if (temp < 10) {
            hexaDeciNum[i] = temp + 48;
            i++;
        }
        else {
            hexaDeciNum[i] = temp + 55;
            i++;
        }

        n = n / 16;
    }

    std::string result;
    for (int j = i - 1; j >= 0; j--) {
        result += hexaDeciNum[j];
    }
    return result;
}
std::string SHA256HashString(std::string aString) {
    std::string digest;
    CryptoPP::SHA256 hash;

    CryptoPP::StringSource foo(aString, true,
        new CryptoPP::HashFilter(hash,
            new CryptoPP::Base64Encoder(
                new CryptoPP::StringSink(digest))));

    return digest;
}
