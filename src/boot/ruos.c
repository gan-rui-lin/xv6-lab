// void print_ruos(void) {

//   // 上边框
//   printfCyan("┌──────────────────────────────┐\n");

//   for (int row = 0; row < 6; ++row) {
//     printfCyan("│ ");
    
//     // 根据行数选择颜色
//     switch (row) {
//         case 0: printfRed("%s%s%s%s", R[row], u[row], O[row], S[row]); break;
//         case 1: printfMagenta("%s%s%s%s", R[row], u[row], O[row], S[row]); break;
//         case 2: printfYellow("%s%s%s%s", R[row], u[row], O[row], S[row]); break;
//         case 3: printfGreen("%s%s%s%s", R[row], u[row], O[row], S[row]); break;
//         case 4: printfCyan("%s%s%s%s", R[row], u[row], O[row], S[row]); break;
//         case 5: printfBlue("%s%s%s%s", R[row], u[row], O[row], S[row]); break;
//     }
    
//     printfCyan(" │\n");
//   }

//   // 下边框
//   printfCyan("└──────────────────────────────┘\n");

// }

#include "defs.h"

void print_ruos(){
    // printf("\033[2J");
    
    // 显示 RUOS ASCII 艺术
    printf("\033[32m"); // 绿色
    printf(" _____             ____     _____ \n");
    printf("|  __ \\           / __ \\   / ____|\n");
    printf("| |__) |  _   _  | |  | | | (___  \n");
    printf("|  _  /  | | | | | |  | |  \\___ \\ \n");
    printf("| | \\ \\  | |_| | | |__| |  ____) |\n");
    printf("|_|  \\_\\  \\__,_|  \\____/  |_____/ \n");

    
    printf("\033[0m"); // 重置颜色

}