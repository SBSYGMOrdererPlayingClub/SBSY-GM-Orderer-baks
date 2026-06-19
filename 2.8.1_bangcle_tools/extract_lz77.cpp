#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

// 返回值：0=成功，其他=错误
int32_t bangcle_lz77_decompress(const uint8_t* in_buf, uint32_t in_len,
                                uint8_t* out_buf, uint32_t* out_len)
{
    uint32_t bit_buf = 0;   // 对应v6，比特缓冲
    uint32_t in_pos = 0;    // 对应v5，输入字节游标
    uint32_t out_pos = 0;   // 对应i，输出字节游标
    uint32_t offset = 1;    // 对应v4，匹配偏移量

    for (;;)
    {
        // ========== 1. 读flag + 字面量循环 ==========
        while (1)
        {
            // 比特缓冲移位或加载新字节
            if ((bit_buf & 0x7F) != 0)
            {
                bit_buf *= 2;
            }
            else
            {
                if (in_pos >= in_len) break;
                bit_buf = 2 * in_buf[in_pos++] + 1;
            }

            // flag=0，跳出字面量循环，进入拷贝块
            if ((bit_buf & 0x100) == 0)
            {
                break;
            }

            // flag=1，直接读取完整字面字节
            if (in_pos >= in_len) break;
            out_buf[out_pos++] = in_buf[in_pos++];
        }

        // ========== 2. 解码长度码 len_code ==========
        uint32_t j = 1;
        uint32_t v16;
        uint32_t len_code;
        for (;;)
        {
            uint32_t v13;
            if ((bit_buf & 0x7F) != 0)
            {
                v13 = 2 * bit_buf;
            }
            else
            {
                if (in_pos >= in_len) goto end_stream;
                v13 = 2 * in_buf[in_pos++] + 1;
            }
            len_code = 2 * j + ((v13 >> 8) & 1);

            if ((v13 & 0x7F) != 0)
            {
                v16 = 2 * v13;
            }
            else
            {
                if (in_pos >= in_len) goto end_stream;
                v16 = 2 * in_buf[in_pos++] + 1;
            }

            // 结束标记为1，长度解码完成
            if ((v16 & 0x100) != 0)
            {
                break;
            }

            // 未结束，更新缓冲和j，继续下一轮
            if ((v16 & 0x7F) != 0)
            {
                bit_buf = 2 * v16;
            }
            else
            {
                if (in_pos >= in_len) goto end_stream;
                bit_buf = 2 * in_buf[in_pos++] + 1;
            }
            j = 2 * len_code - 2 + ((bit_buf >> 8) & 1);
        }

        uint32_t save_in_pos = in_pos;
        uint32_t size_flag;

        // ========== 3. 偏移解码 ==========
        if (len_code != 2)
        {
            // 长偏移
            in_pos++;
            uint32_t raw_off = ((len_code + 16777213u) << 8) + in_buf[save_in_pos];
            
            // 遇到结束标记 0xFFFFFFFF，正常退出
            if (raw_off == 0xFFFFFFFF)
            {
                *out_len = out_pos;
                if (in_pos != in_len)
                {
                    return in_pos < in_len ? -205 : -201;
                }
                return 0;
            }

            offset = (raw_off >> 1) + 1;
            size_flag = !(in_buf[save_in_pos] & 1);
        }
        else
        {
            // 短偏移
            if ((v16 & 0x7F) != 0)
            {
                v16 *= 2;
            }
            else
            {
                in_pos++;
                v16 = 2 * in_buf[save_in_pos] + 1;
            }
            size_flag = (v16 >> 8) & 1;
        }

        // ========== 4. 解码最终拷贝长度 ==========
label_26:
        if ((v16 & 0x7F) != 0)
        {
            bit_buf = 2 * v16;
        }
        else
        {
            if (in_pos >= in_len) goto end_stream;
            bit_buf = 2 * in_buf[in_pos++] + 1;
        }
        int copy_base = 2 * size_flag + ((bit_buf >> 8) & 1);

        if (!copy_base)
        {
            int v24 = 1;
            do
            {
                uint32_t v25;
                if ((bit_buf & 0x7F) != 0)
                {
                    v25 = 2 * bit_buf;
                }
                else
                {
                    if (in_pos >= in_len) goto end_stream;
                    v25 = 2 * in_buf[in_pos++] + 1;
                }
                v24 = 2 * v24 + ((v25 >> 8) & 1);

                if ((v25 & 0x7F) != 0)
                {
                    bit_buf = 2 * v25;
                }
                else
                {
                    if (in_pos >= in_len) goto end_stream;
                    bit_buf = 2 * in_buf[in_pos++] + 1;
                }
            } while ((bit_buf & 0x100) == 0);
            copy_base = v24 + 2;
        }

        // ========== 5. 执行历史拷贝 ==========
        uint32_t copy_len = (offset > 0x500) ? (copy_base + 1) : copy_base;

        uint32_t src_start = out_pos - offset;
        const uint8_t* src = out_buf + src_start;

        // 先拷第1个字节
        out_buf[out_pos] = src[0];

        // 循环拷贝剩余字节
        uint32_t idx = 0;
        uint32_t out_next = out_pos + 1;
        do
        {
            uint8_t val = src[idx + 1];
            uint32_t dst = out_next + idx++;
            out_buf[dst] = val;
        } while (copy_len != idx);

        // 更新输出游标
        out_pos = out_next + copy_len;
    }

end_stream:
    *out_len = out_pos;
    return -3;
}

// 解压后异或修复（对应原汇编loc_16354E）
void bangcle_xor_repair(uint8_t* buf, uint32_t buf_len,
                        uint32_t xor_start, uint32_t xor_key)
{
    uint32_t xor_len = xor_key;
    if (xor_start + xor_len > buf_len)
        xor_len = buf_len - xor_start;

    for (uint32_t i = 0; i + 4 <= xor_len; i += 4)
    {
        uint32_t* p = (uint32_t*)(buf + xor_start + i);
        *p ^= xor_key;
    }
}

// ==================== 主函数 ====================
int main()
{
    const char* input_file  = "compressed_raw.bin";
    const char* output_file = "decompressed.bin";

    // ===== 替换为你从0x1638AF结构体读取的真实DWORD值 =====
    const uint32_t XOR_START_OFFSET = 0x1C;   // 0x1638AF + 0x1C
    const uint32_t XOR_KEY          = 0x0;    // 0x1638AF + 0x20
    // ===================================================

    const uint32_t OUT_BUF_SIZE = 150 * 1024 * 1024; // 150MB缓冲

    // 1. 读取压缩文件
    FILE* fin = fopen(input_file, "rb");
    if (!fin)
    {
        printf("[错误] 无法打开输入文件：%s\n", input_file);
        system("pause");
        return -1;
    }
    fseek(fin, 0, SEEK_END);
    long fsize = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    uint8_t* in_buf = (uint8_t*)malloc(fsize);
    fread(in_buf, 1, fsize, fin);
    fclose(fin);
    printf("[信息] 读取压缩文件：%ld 字节\n", fsize);

    // 2. 分配输出缓冲区
    uint8_t* out_buf = (uint8_t*)malloc(OUT_BUF_SIZE);
    if (!out_buf)
    {
        printf("[错误] 输出缓冲区分配失败\n");
        free(in_buf);
        system("pause");
        return -1;
    }
    memset(out_buf, 0, OUT_BUF_SIZE);

    // 3. 执行解压
    uint32_t out_len = 0;
    int ret = bangcle_lz77_decompress(in_buf, (uint32_t)fsize, out_buf, &out_len);

    printf("\n[结果] 解压返回码：%d\n", ret);
    printf("[结果] 实际输出字节：%u (%.2f MB)\n", out_len, out_len / 1024.0 / 1024.0);

    // 4. 异或修复
    if (out_len > 0)
    {
        bangcle_xor_repair(out_buf, out_len, XOR_START_OFFSET, XOR_KEY);
        printf("[信息] 异或修复完成\n");

        // 5. 写入输出文件
        FILE* fout = fopen(output_file, "wb");
        if (fout)
        {
            fwrite(out_buf, 1, out_len, fout);
            fclose(fout);
            printf("[成功] 已保存解压结果到：%s\n", output_file);
        }
    }

    free(in_buf);
    free(out_buf);
    system("pause");
    return 0;
}
