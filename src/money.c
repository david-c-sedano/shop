/* Formats a price with a dollar sign, commas, and two decimal places. */
void format_money(char* output, size_t output_size, double amount) {
    char number[64];
    snprintf(number, sizeof(number), "%.2f", amount);

    const char* digits = number;
    bool is_negative = false;
    if (number[0] == '-') {
        is_negative = true;
        digits++;
    }

    const char* decimal_point = strchr(digits, '.');
    size_t whole_length = decimal_point ? (size_t)(decimal_point - digits) : strlen(digits);
    const char* decimals = decimal_point ? decimal_point : ".00";

    char formatted_whole[96];
    size_t formatted_index = 0;

    for (size_t digit_index = 0; digit_index < whole_length; digit_index++) {
        if (digit_index > 0 && (whole_length - digit_index) % 3 == 0) {
            formatted_whole[formatted_index++] = ',';
        }
        formatted_whole[formatted_index++] = digits[digit_index];
    }
    formatted_whole[formatted_index] = '\0';

    snprintf(
        output,
        output_size,
        is_negative ? "-$%s%s" : "$%s%s",
        formatted_whole,
        decimals
    );
}
