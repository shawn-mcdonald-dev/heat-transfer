import struct
import sys
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap


def read_matrix(filename):
    with open(filename, "rb") as f:
        rows = struct.unpack("i", f.read(4))[0]
        cols = struct.unpack("i", f.read(4))[0]
        data = np.fromfile(f, dtype=np.float64, count=rows * cols)
        matrix = data.reshape((rows, cols))
    return matrix


def main():
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print(f"Usage: python3 {sys.argv[0]} <input_file> [output_image]")
        sys.exit(1)

    input_file = sys.argv[1]
    output_image = sys.argv[2] if len(sys.argv) == 3 else None

    matrix = read_matrix(input_file)
    rows, cols = matrix.shape

    # 🔵 → 🟣 → 🔴 colormap
    cmap = LinearSegmentedColormap.from_list(
        "blue_purple_red",
        [(0.0, "blue"),
         (0.5, "purple"),
         (1.0, "red")]
    )

    fig, ax = plt.subplots(figsize=(6, 6))

    im = ax.imshow(
        matrix,
        cmap=cmap,
        origin="upper",
        interpolation="nearest",
        vmin=0.0,
        vmax=1.0
    )

    # colorbar
    cbar = plt.colorbar(im, ax=ax)
    cbar.set_label("Temperature")

    # titles + labels
    ax.set_title(f"Heatmap of {input_file}")
    ax.set_xlabel("Column")
    ax.set_ylabel("Row")

    ax.set_xticks(np.arange(cols))
    ax.set_yticks(np.arange(rows))

    # values inside cells (only for small grids)
    if rows <= 10 and cols <= 10:
        for i in range(rows):
            for j in range(cols):
                val = matrix[i, j]
                text_color = "white" if val < 0.7 else "black"
                ax.text(j, i, f"{val:.2f}",
                        ha="center", va="center",
                        color=text_color, fontsize=10)

    plt.tight_layout()

    # save if requested
    if output_image:
        plt.savefig(output_image, dpi=300, bbox_inches="tight")
        print(f"Saved heatmap to {output_image}")

    plt.show()


if __name__ == "__main__":
    main()