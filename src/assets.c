
#define LETTER_BOUNDRY_SIZE     0.25f
#define TEXT_MAX_LAYERS         32
#define LETTER_BOUNDRY_COLOR    VIOLET

bool SHOW_LETTER_BOUNDRY = false;
bool SHOW_TEXT_BOUNDRY = false;

// hold colors here as well
const Color SHOP_BG = { 250, 247, 220, 255 };
const Color SHOP_BG_ALT = { 242, 235, 190, 255 };
const Color SHOP_SHADOW = { 184, 174, 139, 255 };

const Color SHOP_ORANGE = { 255, 139, 35, 255 };
const Color SHOP_ORANGE_LIGHT = { 255, 185, 75, 255 };
const Color SHOP_ORANGE_DARK = { 220, 92, 12, 255 };

const Color SHOP_BLUE = { 28, 180, 220, 255 };
const Color SHOP_GREEN = { 34, 183, 92, 255 };
const Color SHOP_RED = { 225, 32, 48, 255 };
const Color SHOP_PINK = { 245, 35, 190, 255 };
const Color SHOP_YELLOW = { 255, 210, 60, 255 };

const Color SHOP_INK = { 73, 61, 44, 255 };
const Color SHOP_TEXT_MUTED = { 135, 122, 92, 255 };
const Color SHOP_WHITE = { 255, 253, 242, 255 };

Font SHOP_FONT;

// textures
Texture2D LOGO;
Texture2D ACCOUNT_SETTINGS_HOME_ICON;
Texture2D CHAIRS_HOME_ICON;
Texture2D OTHER_FURNISHINGS_HOME_ICON;
Texture2D LARGER_ITEMS_HOME_ICON;
Texture2D HOME_ICON; 

void init_textures() {
    SHOP_FONT = LoadFont("assets/JetBrainsMono-Bold.ttf");

    LOGO = LoadTexture("./assets/PEAK_Software_Logo.png");
    ACCOUNT_SETTINGS_HOME_ICON = LoadTexture("./assets/icons/account_settings_home_icon.png");
    CHAIRS_HOME_ICON = LoadTexture("./assets/icons/chairs_home_icon.png");
    OTHER_FURNISHINGS_HOME_ICON = LoadTexture("./assets/icons/other_furnishings_home_icon.png");
    LARGER_ITEMS_HOME_ICON = LoadTexture("./assets/icons/larger_items_home_icon.png");
    HOME_ICON = LoadTexture("./assets/icons/home_icon.png");
}

Item_Resource* get_item_resource(Shop* shop, Item item) {
    Item_Resource* resource = ht_find(&shop->item_resources, item.id);
    if (resource) {
        return resource;
    }
    Item_Resource new_resource = {0};
    // try png
    const char* png_path = TextFormat(
        "assets/items/%s.png",
        item.name
    );
    if (FileExists(png_path)) {
        new_resource.texture = LoadTexture(png_path);
        new_resource.is_model = false;
        new_resource.scale = 1.5;
        *ht_put(&shop->item_resources, item.id) = new_resource;
        return ht_find(&shop->item_resources, item.id);
    }

    // try glb
    const char* glb_path = TextFormat(
        "assets/items/%s.glb",
        item.name
    );
    if (FileExists(glb_path)) {
        new_resource.model = LoadModel(glb_path);
        new_resource.is_model = true;
        BoundingBox box = GetModelBoundingBox(new_resource.model);
        float width  = box.max.x - box.min.x;
        float height = box.max.y - box.min.y;
        float depth  = box.max.z - box.min.z;
        float max_size = fmaxf(width, fmaxf(height, depth));
        new_resource.scale = 1.5 / max_size;
        *ht_put(&shop->item_resources, item.id) = new_resource;
        return ht_find(&shop->item_resources, item.id);
    }

    printf("no asset found for `%s`\n", item.name);
    return NULL;
}

/*  HELPER CODE FROM RAYLIB DRAWING TEXT IN 3D SPACE 
    https://www.raylib.com/examples/text/loader.html?name=text_3d_drawing
*/

// Draw codepoint at specified position in 3D space
static void DrawTextCodepoint3D(Font font, int codepoint, Vector3 position, float fontSize, bool backface, Color tint)
{
    // Character index position in sprite font
    // NOTE: In case a codepoint is not available in the font, index returned points to '?'
    int index = GetGlyphIndex(font, codepoint);
    float scale = fontSize/(float)font.baseSize;

    // Character destination rectangle on screen
    // NOTE: We consider charsPadding on drawing
    position.x += (float)(font.glyphs[index].offsetX - font.glyphPadding)*scale;
    position.z += (float)(font.glyphs[index].offsetY - font.glyphPadding)*scale;

    // Character source rectangle from font texture atlas
    // NOTE: We consider chars padding when drawing, it could be required for outline/glow shader effects
    Rectangle srcRec = { font.recs[index].x - (float)font.glyphPadding, font.recs[index].y - (float)font.glyphPadding,
                         font.recs[index].width + 2.0f*font.glyphPadding, font.recs[index].height + 2.0f*font.glyphPadding };

    float width = (float)(font.recs[index].width + 2.0f*font.glyphPadding)*scale;
    float height = (float)(font.recs[index].height + 2.0f*font.glyphPadding)*scale;

    if (font.texture.id > 0)
    {
        const float x = 0.0f;
        const float y = 0.0f;
        const float z = 0.0f;

        // normalized texture coordinates of the glyph inside the font texture (0.0f -> 1.0f)
        const float tx = srcRec.x/font.texture.width;
        const float ty = srcRec.y/font.texture.height;
        const float tw = (srcRec.x+srcRec.width)/font.texture.width;
        const float th = (srcRec.y+srcRec.height)/font.texture.height;

        if (SHOW_LETTER_BOUNDRY) DrawCubeWiresV((Vector3){ position.x + width/2, position.y, position.z + height/2}, (Vector3){ width, LETTER_BOUNDRY_SIZE, height }, LETTER_BOUNDRY_COLOR);

        rlCheckRenderBatchLimit(4 + 4*backface);
        rlSetTexture(font.texture.id);

        rlPushMatrix();
            rlTranslatef(position.x, position.y, position.z);

            rlBegin(RL_QUADS);
                rlColor4ub(tint.r, tint.g, tint.b, tint.a);

                // Front Face
                rlNormal3f(0.0f, 1.0f, 0.0f);                                   // Normal Pointing Up
                rlTexCoord2f(tx, ty); rlVertex3f(x,         y, z);              // Top Left Of The Texture and Quad
                rlTexCoord2f(tx, th); rlVertex3f(x,         y, z + height);     // Bottom Left Of The Texture and Quad
                rlTexCoord2f(tw, th); rlVertex3f(x + width, y, z + height);     // Bottom Right Of The Texture and Quad
                rlTexCoord2f(tw, ty); rlVertex3f(x + width, y, z);              // Top Right Of The Texture and Quad

                if (backface)
                {
                    // Back Face
                    rlNormal3f(0.0f, -1.0f, 0.0f);                              // Normal Pointing Down
                    rlTexCoord2f(tx, ty); rlVertex3f(x,         y, z);          // Top Right Of The Texture and Quad
                    rlTexCoord2f(tw, ty); rlVertex3f(x + width, y, z);          // Top Left Of The Texture and Quad
                    rlTexCoord2f(tw, th); rlVertex3f(x + width, y, z + height); // Bottom Left Of The Texture and Quad
                    rlTexCoord2f(tx, th); rlVertex3f(x,         y, z + height); // Bottom Right Of The Texture and Quad
                }
            rlEnd();
        rlPopMatrix();

        rlSetTexture(0);
    }
}

// Draw a 2D text in 3D space
static void DrawText3D(Font font, const char *text, Vector3 position, float fontSize, float fontSpacing, float lineSpacing, bool backface, Color tint)
{
    int length = TextLength(text);          // Total length in bytes of the text, scanned by codepoints in loop

    float textOffsetY = 0.0f;               // Offset between lines (on line break '\n')
    float textOffsetX = 0.0f;               // Offset X to next character to draw

    float scale = fontSize/(float)font.baseSize;

    for (int i = 0; i < length;)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int codepoint = GetCodepoint(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);

        // NOTE: Normally we exit the decoding sequence as soon as a bad byte is found (and return 0x3f)
        // but we need to draw all of the bad bytes using the '?' symbol moving one byte
        if (codepoint == 0x3f) codepointByteCount = 1;

        if (codepoint == '\n')
        {
            // NOTE: Fixed line spacing of 1.5 line-height
            // TODO: Support custom line spacing defined by user
            textOffsetY += fontSize + lineSpacing;
            textOffsetX = 0.0f;
        }
        else
        {
            if ((codepoint != ' ') && (codepoint != '\t'))
            {
                DrawTextCodepoint3D(font, codepoint, (Vector3){ position.x + textOffsetX, position.y, position.z + textOffsetY }, fontSize, backface, tint);
            }

            if (font.glyphs[index].advanceX == 0) textOffsetX += (float)font.recs[index].width*scale + fontSpacing;
            else textOffsetX += (float)font.glyphs[index].advanceX*scale + fontSpacing;
        }

        i += codepointByteCount;   // Move text bytes counter to next codepoint
    }
}

void DrawTextCentered3D(
    Font font,
    const char* text,
    Vector3 pos,
    float font_size,
    float spacing,
    Color tint
) {
    Vector2 size = MeasureTextEx(font, text, font_size, spacing);

    // instead of thinking about `DrawText3D` and doing it properly
    // just rotate XZ -> XY and be done with it
    rlPushMatrix();
        rlTranslatef(pos.x, pos.y, pos.z);
        rlRotatef(90.0, 1.0, 0.0, 0.0);
        DrawText3D(
            font, 
            text, 
            (Vector3){ -size.x * 0.5, 0.0, 0.0 },
            font_size,
            spacing,
            0.0,
            false,
            tint
        );
    rlPopMatrix();
}

// It doesnt matter where, it doesn't matter when
// typographical problems will always rear its ugly *** and try to **** me over
void DrawTextWordWrapped3D(
    Font font,
    const char* text,
    Vector3 pos,
    float max_width,
    float font_size,
    float spacing,
    float line_spacing,
    Color tint
) {
    // maybe an arena makes more sense
    // but I am willing to bet like, 50 dollars these dont overflow
    char line[500] = {0};
    char test[500] = {0};
    char word[100] = {0};
    float y = 0.0;

    const char* p = text;
    while (*p) {
        while (*p == ' ') {
            p+=1;
        }
        if (!*p) {
            break;
        }
        // read word
        int word_len = 0;
        while (
            p[word_len]         &&
            p[word_len] != ' '  &&
            p[word_len] != '\n' &&
            word_len < (int)sizeof(word)-1
        ) {
            word[word_len] = p[word_len];
            word_len+=1;
        }

        word[word_len] = 0;
        if (line[0]) {
            snprintf(test, sizeof(test), "%s %s", line, word);
        } else {
            snprintf(test, sizeof(test), "%s", word);
        }
        float width = MeasureTextEx(font, test, font_size, spacing).x;

        // doesnt fit
        if (line[0] && width > max_width) {
            DrawTextCentered3D(
                font, line, 
                (Vector3){ pos.x, pos.y - y, pos.z }, 
                font_size, spacing, tint
            );
            y += font_size + line_spacing;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", test);
        }
        p += word_len;

        if (*p == '\n') {
            /// TODO? ehhhh who needs this crap?
        }
    }

    if (line[0]) {
        DrawTextCentered3D(
            font, line,
            (Vector3){ pos.x, pos.y - y, pos.z },
            font_size, spacing, tint
        );
    }
}
