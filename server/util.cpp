#include "util.hpp"

// Gets env variable
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
    // char array to store hexadecimal number 
    char hexaDeciNum[100];

    // Counter for hexadecimal number array 
    int i = 0;
    while (n != 0) {
        // Temporary variable to store remainder 
        size_t temp = 0;

        // Storing remainder in temp variable. 
        temp = n % 16;

        // Check if temp < 10 
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

    // Prsize_ting hexadecimal number 
    // array in reverse order 
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
// Returns redis connection options to be used in redis connection creation
//sw::redis::ConnectionOptions getRedisConnectionOptions() {
//    std::string REDIS_HOST;
//    std::string REDIS_PORT;
//    std::string REDIS_PASSWORD;
//    sw::redis::ConnectionOptions opts;
//    if (getenv("REDIS_HOST", REDIS_HOST))
//        opts.host = REDIS_HOST;
//    if (getenv("REDIS_PORT", REDIS_PORT))
//        opts.port = std::stoi(REDIS_PORT);
//    if (getenv("REDIS_PASSWORD", REDIS_PASSWORD))
//        opts.password = REDIS_PASSWORD;
//    opts.socket_timeout = std::chrono::milliseconds(200);
//    return opts;
//}