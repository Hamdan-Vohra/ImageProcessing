#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <png.h>
#include <omp.h>

#define IMAGESNO 300
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

// Suppress PNG warnings
void custom_warning_handler(png_structp png_ptr, png_const_charp warning_msg) {}

// Free allocated image data
void free_image_data(ImageData *img) {
    if (!img || !img->row_pointers) return;
    free(img->row_pointers[0]);
    free(img->row_pointers);
    img->row_pointers = NULL;
}

// Copy image data to a new structure
void copy_image_data(ImageData *src, ImageData *dest) {
    dest->width = src->width;
    dest->height = src->height;
    dest->color_type = src->color_type;
    dest->bit_depth = src->bit_depth;

    size_t row_size = dest->width * 4; // Assuming RGBA
    dest->row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * dest->height);
    png_bytep pixel_data = (png_bytep)malloc(row_size * dest->height);

    #pragma omp parallel for
    for (int y = 0; y < dest->height; y++) {
        dest->row_pointers[y] = &pixel_data[y * row_size];
        memcpy(dest->row_pointers[y], src->row_pointers[y], row_size);
    }
}

// Initialize the mask matrix in parallel
void initialize_mask() {
    maskMatrix = (int **)malloc(maskDimensions * sizeof(int *));
    #pragma omp parallel for
    for (int i = 0; i < maskDimensions; i++) {
        maskMatrix[i] = (int *)malloc(maskDimensions * sizeof(int));
        #pragma omp parallel for
        for (int j = 0; j < maskDimensions; j++) {
            maskMatrix[i][j] = 1;
        }
    }
}

// Read a PNG image
void read_png_file(char *filename, ImageData *img) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("File not found: %s. Skipping...\n", filename);
        img->width = 0;
        return;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_set_error_fn(png, NULL, NULL, custom_warning_handler);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, fp);
    png_read_info(png, info);

    img->width = png_get_image_width(png, info);
    img->height = png_get_image_height(png, info);
    img->color_type = png_get_color_type(png, info);
    img->bit_depth = png_get_bit_depth(png, info);

    if (img->bit_depth == 16) png_set_strip_16(png);
    if (img->color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    png_set_gray_to_rgb(png);
    png_read_update_info(png, info);

    img->row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * img->height);
    png_bytep pixel_data = (png_bytep)malloc(png_get_rowbytes(png, info) * img->height);

    #pragma omp parallel for
    for (int y = 0; y < img->height; y++) {
        img->row_pointers[y] = &pixel_data[y * png_get_rowbytes(png, info)];
    }

    png_read_image(png, img->row_pointers);
    fclose(fp);
    png_destroy_read_struct(&png, &info, NULL);
}

// Write a PNG image
void write_png_file(char *filename, ImageData *img) {
    FILE *fp = fopen(filename, "wb");
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, fp);
    png_set_IHDR(png, info, img->width, img->height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_write_image(png, img->row_pointers);
    png_write_end(png, NULL);
    fclose(fp);
    png_destroy_write_struct(&png, &info);
}

// Invert image colors in parallel
void invert_colors(ImageData *img) {
    #pragma omp parallel for collapse(2) // Combine two loops
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            png_bytep px = &(img->row_pointers[y][x * 4]);
            px[0] = 255 - px[0];
            px[1] = 255 - px[1];
            px[2] = 255 - px[2];
        }
    }
}

// Add a brightness filter
void adjust_brightness(ImageData *img, int brightness) {
    #pragma omp parallel for collapse(2) schedule(dynamic, 4)
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            png_bytep px = &(img->row_pointers[y][x * 4]);
            px[0] = (px[0] + brightness) > 255 ? 255 : px[0] + brightness;
            px[1] = (px[1] + brightness) > 255 ? 255 : px[1] + brightness;
            px[2] = (px[2] + brightness) > 255 ? 255 : px[2] + brightness;
        }
    }
}

// Add a grayscale filter
void apply_grayscale(ImageData *img) {
    #pragma omp parallel for collapse(2)
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            png_bytep px = &(img->row_pointers[y][x * 4]);
            int gray = (px[0] + px[1] + px[2]) / 3;
            px[0] = px[1] = px[2] = gray;
        }
    }
}

void maskRGB(ImageData *img) {
    int maskD = maskDimensions * maskDimensions;
    int size = img->height * img->width;
    int *maskedImageR = (int*)malloc(size * sizeof(int));
    int *maskedImageG = (int*)malloc(size * sizeof(int));
    int *maskedImageB = (int*)malloc(size * sizeof(int));

    #pragma omp parallel for
    for (int i = 0; i < img->height; i++) {
        for (int j = 0; j < img->width; j++) {
            int averageR = 0, averageG = 0, averageB = 0;

            for (int itImageX = i - maskDimensions / 2, itMaskX = 0;
                 itImageX <= (i + maskDimensions / 2) && itMaskX < maskDimensions;
                 itImageX++, itMaskX++) {

                if (itImageX >= 0 && itImageX < img->height) {
                    png_bytep row = img->row_pointers[itImageX];
                    for (int itImageY = j - maskDimensions / 2, itMaskY = 0;
                         itImageY <= (j + maskDimensions / 2) && itMaskY < maskDimensions;
                         itImageY++, itMaskY++) {

                        if (itImageY >= 0 && itImageY < img->width) {
                            png_bytep px = &(row[itImageY * 4]);
                            averageR += px[0] * maskMatrix[itMaskX][itMaskY];
                            averageG += px[1] * maskMatrix[itMaskX][itMaskY];
                            averageB += px[2] * maskMatrix[itMaskX][itMaskY];
                        }
                    }
                }
            }

            maskedImageR[i * img->width + j] = averageR / maskD;
            maskedImageG[i * img->width + j] = averageG / maskD;
            maskedImageB[i * img->width + j] = averageB / maskD;
        }
    }

    int x = 0;
    for (int i = 0; i < img->height; i++) {
        png_bytep row = img->row_pointers[i];
        for (int j = 0; j < img->width; j++) {
            png_bytep px = &(row[j * 4]);
            px[0] = maskedImageR[x];
            px[1] = maskedImageG[x];
            px[2] = maskedImageB[x];
            x++;
        }
    }

    free(maskedImageR);
    free(maskedImageG);
    free(maskedImageB);
}

// Main function with batch-level parallelism
#include <omp.h> // For time calculation and parallelization
#include <stdio.h> // For printf
#include <stdlib.h> // For system commands

int main() {
    omp_set_num_threads(8); // Set the number of threads
    char *fileDirectory = "../DataSet";
    char command[256];
    snprintf(command, sizeof(command), "test -d \"%s\"", fileDirectory);
    if (system(command)) {
        printf("DataSet Doesn't exist\n");
        return 1;
    }

    printf("Enter the size of Mask:\n");
    scanf("%d", &maskDimensions);

    // Start timer
    double start_time = omp_get_wtime();

    system("mkdir -p NegationImages BlurImages GrayscaleImages BrightnessImages");
    initialize_mask();


    #pragma omp parallel for schedule(static, 2)
    for (int i = 0; i < IMAGESNO; i++) {
        ImageData img;
        char inputFile[100];
        sprintf(inputFile, "%s/%d.png", fileDirectory, i + 1);
        read_png_file(inputFile, &img);

        if (img.width == 0) continue;

        #pragma omp task
        {
            ImageData negation_img;
            copy_image_data(&img, &negation_img);
            invert_colors(&negation_img);
            char outputFile[100];
            sprintf(outputFile, "NegationImages/%d.png", i + 1);
            write_png_file(outputFile, &negation_img);
            free_image_data(&negation_img);
        }

        #pragma omp task
        {
            ImageData brightness_img;
            copy_image_data(&img, &brightness_img);
            adjust_brightness(&brightness_img, 20);
            char outputFile[100];
            sprintf(outputFile, "BrightnessImages/%d.png", i + 1);
            write_png_file(outputFile, &brightness_img);
            free_image_data(&brightness_img);
        }

        #pragma omp task
        {
            ImageData grayscale_img;
            copy_image_data(&img, &grayscale_img);
            apply_grayscale(&grayscale_img);
            char outputFile[100];
            sprintf(outputFile, "GrayscaleImages/%d.png", i + 1);
            write_png_file(outputFile, &grayscale_img);
            free_image_data(&grayscale_img);
        }

        #pragma omp task
        {
            ImageData blur_img;
            copy_image_data(&img, &blur_img);
            maskRGB(&blur_img);
            char outputFile[100];
            sprintf(outputFile, "BlurImages/%d.png", i + 1);
            write_png_file(outputFile, &blur_img);
            free_image_data(&blur_img);
        }

    }


    // End timer
    double end_time = omp_get_wtime();
    printf("Image Processing Done!\n");
    printf("Total Time Taken: %.2f seconds\n", end_time - start_time);

    return 0;
}
