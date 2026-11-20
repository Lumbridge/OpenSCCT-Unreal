"""Registered restoration from the original Hospital signs and grille, with no redrawn graphics."""
import json

import cv2
import numpy as np
from PIL import Image, ImageDraw


SIGN_RECTS = {
    'Prison_TEX_pan_hospital.png': [(28, 11, 104, 49), (151, 11, 235, 49), (15, 65, 111, 104),
                                   (139, 65, 241, 104), (39, 130, 222, 237)],
    'Prison_TEX_pan_danger.png': [(28, 31, 223, 80), (12, 96, 249, 182), (28, 191, 239, 252)],
    'Prison_TEX_pan_end.png': [(28, 18, 226, 98), (29, 106, 221, 174), (13, 202, 243, 253)],
    'Prison_TEX_pan_end1.png': [(28, 18, 226, 98), (29, 106, 221, 174), (13, 202, 243, 253)],
}
GRILLE = 'Prison_TEX_trouegouts.png'
SCALE = 8


def restore(image, areas):
    rgba = np.asarray(image.convert('RGBA'))
    rgb = rgba[..., :3].astype(np.float32)
    high_size = (image.width * SCALE, image.height * SCALE)
    guide = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY)

    # Remove only weak codec steps. Large or sustained edges, engraved shapes and wear retain source values.
    blurred = cv2.bilateralFilter(rgb, 5, 11.0, 1.05)
    gradient = cv2.dilate(cv2.magnitude(cv2.Sobel(guide, cv2.CV_32F, 1, 0, ksize=3),
                                       cv2.Sobel(guide, cv2.CV_32F, 0, 1, ksize=3)),
                          np.ones((3, 3), np.uint8)) / 8.0
    keep = np.clip((gradient - 4.0) / 17.0, 0.0, 1.0)
    cleaned = rgb + (blurred - rgb) * (.66 * (1.0 - keep[..., None]))
    base = cv2.resize(cleaned, high_size, interpolation=cv2.INTER_CUBIC)
    bound_kernel = np.ones((3, 3), np.uint8)
    lower = cv2.resize(cv2.erode(rgb, bound_kernel), high_size, interpolation=cv2.INTER_NEAREST)
    upper = cv2.resize(cv2.dilate(rgb, bound_kernel), high_size, interpolation=cv2.INTER_NEAREST)
    base = np.clip(base, lower, upper)

    # Refine existing silhouettes at their original texel centers. No thresholded masks or new outlines.
    masks = np.zeros((image.height, image.width), np.float32)
    for l, t, r, b in areas:
        masks[t:b, l:r] = 1.0
    masks = cv2.GaussianBlur(masks, (0, 0), 1.25)
    amount = cv2.resize(masks * (.57 + .38 * keep), high_size, interpolation=cv2.INTER_LINEAR)
    shoulder = cv2.GaussianBlur(base, (0, 0), sigmaX=2.45, sigmaY=2.45)
    residual = np.clip(base - shoulder, -24, 24)
    refined = np.clip(base + residual * amount[..., None], lower, upper)
    alpha = np.asarray(Image.fromarray(rgba[..., 3]).resize(high_size, Image.Resampling.NEAREST))
    pixels = np.dstack((np.rint(refined).astype(np.uint8), alpha))
    return Image.fromarray(pixels)


def preview(source, output, files):
    sheet = Image.new('RGB', (1070, 1350), (31, 32, 31))
    draw = ImageDraw.Draw(sheet)
    names = ['Original source scaled to preview', 'Registered restoration']
    for index, title in enumerate(names):
        draw.text((8 + index * 535, 8), title, fill='white')
    for idx, name in enumerate(files):
        pair = (source / name, output / name)
        row, column = divmod(idx, 2)
        for side, path in enumerate(pair):
            image = Image.open(path).convert('RGB')
            image = image.resize((252, int(252 * image.height / image.width)), Image.Resampling.NEAREST if side == 0 else Image.Resampling.LANCZOS)
            x, y = 8 + side * 535 + column * 263, 40 + row * 280
            draw.text((x, y - 13), name, fill='white')
            sheet.paste(image, (x, y))
    sheet.crop((0, 0, 1070, 760)).save(output / 'Comparison.png')


def run(source, output):
    output.mkdir(parents=True, exist_ok=True)
    for name, areas in (*SIGN_RECTS.items(), (GRILLE, [(128, 0, 256, 128)])):
        with Image.open(source / name) as original:
            level = restore(original, areas)
            assert level.size == (original.width * SCALE, original.height * SCALE)
            assert np.array_equal(np.asarray(level.getchannel('A')),
                                  np.asarray(original.convert('RGBA').getchannel('A').resize(level.size, Image.Resampling.NEAREST)))
        level.save(output / name)
        print(name, level.size, 'exact original alpha')
    preview(source, output, (*SIGN_RECTS.keys(), GRILLE))
    (output / 'manifest.json').write_text(json.dumps({name: name for name in SIGN_RECTS}, indent=2) + '\n', encoding='utf-8')
