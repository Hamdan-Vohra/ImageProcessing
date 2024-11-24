#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <png.h>
#include <omp.h>

#define IMAGESNO 2000
#define BATCH_SIZE 100 // Number of images to process in one batch

typedef struct {
    int width;
    int height;
    png_byte color_type;
    png_byte bit_depth;
    png_bytep *row_pointers;
} ImageData;

int maskDimensions;
int **maskMatrix;

void custom_warning_handler(png_structp png_ptr, png_const_charp warning_msg) {
    // Suppress warning messages
}

void free_image_data(ImageData *img) {
    if (!img || !img->row_pointers) return;
    free(img->row_pointers[0]); // Free contiguous pixel data
    free(img->row_pointers);    // Free row pointers
    img->row_pointers = NULL;
}

void copy_image_data(ImageData *src, ImageData *dest) {
    dest->width = src->width;
    dest->height = src->height;
    dest->color_type = src->color_type;
    dest->bit_depth = src->bit_depth;

    size_t row_size = dest->width * 4; // Assuming RGBA
    dest->row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * dest->height);
    png_bytep pixel_data = (png_bytep)malloc(row_size * dest->height);

    if (!dest->row_pointers || !pixel_data) {
        fprintf(stderr, "Memory allocation failed during copy_image_data.\n");
        exit(EXIT_FAILURE);
    }

    for (int y = 0; y < dest->height; y++) {
        dest->row_pointers[y] = &pixel_data[y * row_size];
        memcpy(dest->row_pointers[y], src->row_pointers[y], row_size);
    }
}

void read_png_file(char *filename, ImageData *img) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("File not found: %s. Skipping...\n", filename);
        img->width = 0;
        return;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) abort();

    png_set_error_fn(png, NULL, NULL, custom_warning_handler);

    png_infop info = png_create_info_struct(png);
    if (!info) abort();

    if (setjmp(png_jmpbuf(png))) abort();

    png_init_io(png, fp);
    png_read_info(png, info);

    img->width = png_get_image_width(png, info);
    img->height = png_get_image_height(png, info);
    img->color_type = png_get_color_type(png, info);
    img->bit_depth = png_get_bit_depth(png, info);

    if (img->bit_depth == 16) png_set_strip_16(png);
    if (img->color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (img->color_type == PNG_COLOR_TYPE_GRAY && img->bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (img->color_type == PNG_COLOR_TYPE_RGB || img->color_type == PNG_COLOR_TYPE_GRAY || img->color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (img->color_type == PNG_COLOR_TYPE_GRAY || img->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    img->row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * img->height);
    png_bytep pixel_data = (png_bytep)malloc(png_get_rowbytes(png, info) * img->height);

    if (!img->row_pointers || !pixel_data) {
        fprintf(stderr, "Memory allocation failed during read_png_file.\n");
        exit(EXIT_FAILURE);
    }

    for (int y = 0; y < img->height; y++) {
        img->row_pointers[y] = &pixel_data[y * png_get_rowbytes(png, info)];
    }

    png_read_image(png, img->row_pointers);

    fclose(fp);
    png_destroy_read_struct(&png, &info, NULL);
}

void write_png_file(char *filename, ImageData *img) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open file for writing: %s\n", filename);
        return;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) abort();

    png_infop info = png_create_info_struct(png);
    if (!info) abort();

    if (setjmp(png_jmpbuf(png))) abort();

    png_init_io(png, fp);
    png_set_IHDR(
        png, info,
        img->width, img->height,
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );
    png_write_info(png, info);
    png_write_image(png, img->row_pointers);
    png_write_end(png, NULL);

    fclose(fp);
    png_destroy_write_struct(&png, &info);
}

void invert_colors(ImageData *img) {
    #pragma omp parallel for
    for (int y = 0; y < img->height; y++) {
        png_bytep row = img->row_pointers[y];
        for (int x = 0; x < img->width; x++) {
            png_bytep px = &(row[x * 4]);
            px[0] = 255 - px[0];
            px[1] = 255 - px[1];
            px[2] = 255 - px[2];
        }
    }
}

void blur_image(ImageData *img, ImageData *blurredImg) {
    int offset = maskDimensions / 2;

    #pragma omp parallel for
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            int sumR = 0, sumG = 0, sumB = 0, maskSum = 0;

            for (int ky = -offset; ky <= offset; ky++) {
                for (int kx = -offset; kx <= offset; kx++) {
                    int ny = y + ky;
                    int nx = x + kx;

                    if (ny >= 0 && ny < img->height && nx >= 0 && nx < img->width) {
                        png_bytep px = &(img->row_pointers[ny][nx * 4]);
                        int maskValue = maskMatrix[ky + offset][kx + offset];
                        sumR += px[0] * maskValue;
                        sumG += px[1] * maskValue;
                        sumB += px[2] * maskValue;
                        maskSum += maskValue;
                    }
                }
            }

            png_bytep outputPx = &(blurredImg->row_pointers[y][x * 4]);
            outputPx[0] = sumR / maskSum;
            outputPx[1] = sumG / maskSum;
            outputPx[2] = sumB / maskSum;
            outputPx[3] = 255; // Alpha channel
        }
    }
}

void initialize_mask() {
    maskMatrix = (int**)malloc(maskDimensions * sizeof(int*));
    for (int i = 0; i < maskDimensions; i++) {
        maskMatrix[i] = (int*)malloc(maskDimensions * sizeof(int));
        for (int j = 0; j < maskDimensions; j++) {
            maskMatrix[i][j] = 1;
        }
    }
}

int main() {
    char *fileDirectory = "../DataSet";
    char command[256];
    snprintf(command, sizeof(command), "test -d \"%s\"", fileDirectory);

    if (system(command)) {
        printf("DataSet Doesn't exist\n");
        return 1;
    }

    omp_set_num_threads(4); // Adjust threads based on your system
    printf("Enter the size of Mask:\n");
    scanf("%d", &maskDimensions);

    system("mkdir -p NegationImages");
    system("mkdir -p BlurImages");

    initialize_mask();
    double startTotalTime = omp_get_wtime();
    
    for (int batch_start = 0; batch_start < IMAGESNO; batch_start += BATCH_SIZE) {
        int batch_end = batch_start + BATCH_SIZE;
        if (batch_end > IMAGESNO) batch_end = IMAGESNO;

        ImageData images[BATCH_SIZE], negateImages[BATCH_SIZE], blurImages[BATCH_SIZE];

        for (int i = batch_start; i < batch_end; i++) {
            char fileName[100];
            sprintf(fileName, "%s/%d.png", fileDirectory, i + 1);
            read_png_file(fileName, &images[i - batch_start]);

            if (images[i - batch_start].width == 0) continue;

            copy_image_data(&images[i - batch_start], &negateImages[i - batch_start]);
            invert_colors(&negateImages[i - batch_start]);

            char negateFileName[100];
            sprintf(negateFileName, "NegationImages/%d.png", i + 1);
            write_png_file(negateFileName, &negateImages[i - batch_start]);

            copy_image_data(&images[i - batch_start], &blurImages[i - batch_start]);
            blur_image(&images[i - batch_start], &blurImages[i - batch_start]);

            char blurFileName[100];
            sprintf(blurFileName, "BlurImages/%d.png", i + 1);
            write_png_file(blurFileName, &blurImages[i - batch_start]);

            free_image_data(&images[i - batch_start]);
            free_image_data(&negateImages[i - batch_start]);
            free_image_data(&blurImages[i - batch_start]);
        }
    }

    double totalTime = omp_get_wtime() - startTotalTime;
    printf("Time Taken for Processing Images: %.4lf seconds, and %.2lf minutes\n",totalTime,totalTime/60.0);
    return 0;
}

