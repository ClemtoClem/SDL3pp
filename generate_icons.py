import os
from PIL import Image, ImageDraw, ImageFont

# Configuration du chemin de sortie de manière sécurisée (compatible tout OS)
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
PATH = os.path.join(BASE_DIR, "assets", "textures", "icons")

# Définition des constantes de couleur et de taille
SIZE = (32, 32)
COLOR_TRANSPARENT = (0, 0, 0, 0)
COLOR_OUTLINE = (70, 130, 210, 255)
COLOR_FILL = (50, 52, 72, 255)
LINE_WIDTH = 2

# Chargement d'une police pour le texte (essaie Arial, sinon police par défaut PIL)
try:
    # Taille 10 ou 11 est idéale pour rentrer 3 lettres dans une icône 32x32
    FONT = ImageFont.truetype(f"{BASE_DIR}/assets/fonts/arial.ttf", 10) 
except IOError:
    FONT = ImageFont.load_default()

# Liste des icônes à générer
ICONS = [
    # --- Fichiers et Édition de base ---
    "icon_open", "icon_save", "icon_save_as", "icon_new", 
    "icon_import", "icon_export",
    "icon_undo", "icon_redo", "icon_print", 
    
    # --- Outils de dessin ---
    "icon_pencil", "icon_brush", "icon_fill", "icon_erase", "icon_select", 
    
    # --- Navigation (Flèches) ---
    "icon_left_arrow", "icon_right_arrow", "icon_up_arrow", "icon_down_arrow",

    # --- ÉDITEUR DE TILESET / TILEMAP ---
    "icon_grid",         # Afficher/Masquer la grille
    "icon_stamp",        # Outil tampon (placer un motif de tuiles)
    "icon_eyedropper",   # Pipette (sélectionner une tuile depuis la map)
    "icon_zoom_in",      # Zoom avant
    "icon_zoom_out",     # Zoom arrière
    "icon_layer_add",    # Ajouter un calque
    "icon_layer_remove", # Supprimer un calque
    "icon_visibility",   # Œil (afficher/masquer le calque)
    "icon_flip_h",       # Miroir horizontal
    "icon_flip_v",       # Miroir vertical
    "icon_rotate",       # Rotation (90 degrés)
    "icon_collision",    # Édition des masques de collision
    "icon_settings",     # Paramètres / Propriétés de la map
    "icon_play",         # Tester la map
    "icon_magic_wand",   # Baguette magique (sélection par tuiles contiguës)
    
    # --- Presse-papiers ---
    "icon_cut", "icon_copy", "icon_paste",
    
    # --- Formatage de texte ---
    "icon_bold", "icon_italic", "icon_underline", "icon_strikethrough",
    "icon_text_color", "icon_highlight",
    
    # --- Paragraphe et Alignement ---
    "icon_align_left", "icon_align_center", "icon_align_right", "icon_align_justify",
    "icon_bullet_list", "icon_number_list", 
    "icon_indent_increase", "icon_indent_decrease",
    
    # --- Insertion ---
    "icon_insert_image", "icon_insert_table", "icon_insert_link", "icon_find",
    
    # --- Dossiers et fichiers spécifiques ---
    "icon_folder", "icon_file", 
    "icon_file_code", "icon_file_xml", "icon_file_json", "icon_file_yaml", 
    "icon_file_ini", "icon_file_image", "icon_file_png", "icon_file_jpg", 
    "icon_file_svg", "icon_file_video", "icon_file_mp3", "icon_file_wav", 
    "icon_file_midi", "icon_file_mp4", "icon_file_mkv", "icon_file_archive", 
    "icon_file_pdf", "icon_file_text", "icon_file_bin",

    # --- LECTEUR MULTIMEDIA ---
    "icon_play", "icon_pause", "icon_stop", 
    "icon_next", "icon_prev", "icon_fast_forward", "icon_rewind",
    "icon_volume_up", "icon_volume_down", "icon_volume_mute",
    "icon_shuffle", "icon_repeat", "icon_fullscreen", "icon_minimize",

    # --- MÉTÉO ---
    "icon_weather_sun", "icon_weather_cloud", "icon_weather_partly_cloudy",
    "icon_weather_rain", "icon_weather_storm", "icon_weather_snow",
    "icon_weather_wind", "icon_weather_thermometer", "icon_weather_humidity"
]

def draw_shape(draw, shape_type, coords, width=LINE_WIDTH, fill=COLOR_FILL, outline=COLOR_OUTLINE):
    """Fonction utilitaire pour dessiner les formes vectorielles."""
    # Pillow gère nativement les listes de points pour les lignes continues
    if shape_type in ("line", "lines"):
        draw.line(coords, fill=outline, width=width, joint="curve")
    elif shape_type == "polygon":
        draw.polygon(coords, fill=fill, outline=outline, width=width)
    elif shape_type == "rectangle":
        draw.rectangle(coords, fill=fill, outline=outline, width=width)
    elif shape_type == "ellipse":
        draw.ellipse(coords, fill=fill, outline=outline, width=width)
    elif shape_type == "arc":
        draw.arc(coords, start=180, end=360, fill=outline, width=width+1)

def draw_cloud(draw, offset_y=0):
    """Dessine un nuage vectoriel composé d'arcs de cercle."""
    oy = offset_y
    
    # 1. Remplissage du nuage (formes pleines pour masquer l'arrière-plan)
    # Cercle gauche, cercle droit, cercle haut, et un rectangle pour boucher le trou en bas
    draw.ellipse([(6, 16+oy), (16, 26+oy)], fill=COLOR_FILL)
    draw.ellipse([(16, 16+oy), (26, 26+oy)], fill=COLOR_FILL)
    draw.ellipse([(10, 10+oy), (22, 22+oy)], fill=COLOR_FILL)
    draw.rectangle([(11, 16+oy), (21, 26+oy)], fill=COLOR_FILL)
    
    # 2. Tracé des contours extérieurs avec des arcs
    # (Pillow : 0° = droite, 90° = bas, 180° = gauche, 270° = haut)
    
    # Arc gauche (du bas vers le haut-droit)
    draw.arc([(6, 16+oy), (16, 26+oy)], start=90, end=315, fill=COLOR_OUTLINE, width=LINE_WIDTH)
    
    # Arc central haut (de la gauche vers la droite)
    draw.arc([(10, 10+oy), (22, 22+oy)], start=190, end=350, fill=COLOR_OUTLINE, width=LINE_WIDTH)
    
    # Arc droit (du haut-gauche vers le bas)
    draw.arc([(16, 16+oy), (26, 26+oy)], start=225, end=90, fill=COLOR_OUTLINE, width=LINE_WIDTH)
    
    # Ligne de base plate
    draw.line([(11, 26+oy), (21, 26+oy)], fill=COLOR_OUTLINE, width=LINE_WIDTH)

def generate_icon(icon_name):
    # Création d'une image vierge avec fond transparent
    img = Image.new('RGBA', SIZE, COLOR_TRANSPARENT)
    draw = ImageDraw.Draw(img)

    # ==========================================
    # ICÔNES DE BASE
    # ==========================================
    if icon_name == "icon_open":
        draw_shape(draw, "polygon", [(4, 10), (12, 10), (16, 14), (28, 14), (28, 26), (4, 26)])
        draw_shape(draw, "line", [(4, 16), (28, 16)])
    elif icon_name == "icon_save":
        draw_shape(draw, "polygon", [(6, 4), (22, 4), (28, 10), (28, 28), (6, 28)])
        draw_shape(draw, "rectangle", [(10, 18), (24, 28)])
        draw_shape(draw, "rectangle", [(12, 4), (20, 10)])
    elif icon_name == "icon_save_as":
        draw_shape(draw, "polygon", [(6, 4), (22, 4), (28, 10), (28, 28), (6, 28)])
        draw_shape(draw, "rectangle", [(10, 18), (24, 28)])
        draw.line([(21, 20), (21, 27)], fill=(255, 255, 255, 255), width=2)
        draw.line([(18, 23), (25, 23)], fill=(255, 255, 255, 255), width=2)
    elif icon_name == "icon_new":
        draw_shape(draw, "polygon", [(8, 4), (18, 4), (24, 10), (24, 28), (8, 28)])
        draw_shape(draw, "polygon", [(18, 4), (18, 10), (24, 10)])
        draw.line([(21, 20), (21, 27)], fill=(255, 255, 255, 255), width=2)
        draw.line([(18, 23), (25, 23)], fill=(255, 255, 255, 255), width=2)
    elif icon_name == "icon_import":
        draw_shape(draw, "polygon", [(6, 18), (26, 18), (26, 28), (6, 28)])
        draw_shape(draw, "polygon", [(12, 8), (20, 8), (20, 14), (24, 14), (16, 22), (8, 14), (12, 14)])
    elif icon_name == "icon_export":
        draw_shape(draw, "polygon", [(6, 18), (26, 18), (26, 28), (6, 28)])
        draw_shape(draw, "polygon", [(12, 22), (20, 22), (20, 14), (24, 14), (16, 6), (8, 14), (12, 14)])
    elif icon_name == "icon_undo":
        draw_shape(draw, "arc", [(6, 12), (26, 24)])
        draw_shape(draw, "polygon", [(4, 18), (10, 12), (10, 24)])
    elif icon_name == "icon_redo":
        draw_shape(draw, "arc", [(6, 12), (26, 24)])
        draw_shape(draw, "polygon", [(28, 18), (22, 12), (22, 24)])
    elif icon_name == "icon_print":
        draw_shape(draw, "polygon", [(10, 4), (22, 4), (22, 12), (10, 12)])
        draw_shape(draw, "rectangle", [(4, 12), (28, 22)])
        draw_shape(draw, "polygon", [(8, 20), (24, 20), (24, 28), (8, 28)])
        draw_shape(draw, "line", [(12, 24), (20, 24)], width=1)
    
    # ==========================================
    # OUTILS DE DESSIN
    # ==========================================
    elif icon_name == "icon_pencil":
        draw_shape(draw, "polygon", [(24, 6), (28, 10), (12, 26), (8, 22)])
        draw_shape(draw, "polygon", [(8, 22), (12, 26), (6, 28)])
    elif icon_name == "icon_brush":
        draw_shape(draw, "polygon", [(22, 8), (26, 12), (16, 22), (12, 18)])
        draw_shape(draw, "polygon", [(12, 18), (16, 22), (12, 28), (6, 28), (6, 22)])
    elif icon_name == "icon_fill":
        draw_shape(draw, "polygon", [(8, 16), (20, 8), (26, 16), (14, 24)])
        draw_shape(draw, "polygon", [(14, 24), (8, 28), (10, 28)])
    elif icon_name == "icon_erase":
        draw_shape(draw, "polygon", [(10, 16), (22, 8), (28, 16), (16, 24)])
        draw_shape(draw, "polygon", [(6, 24), (16, 24), (10, 16)])
    elif icon_name == "icon_select":
        for i in range(6, 26, 4):
            draw.line([(i, 6), (i+2, 6)], fill=COLOR_OUTLINE, width=LINE_WIDTH)
            draw.line([(i, 26), (i+2, 26)], fill=COLOR_OUTLINE, width=LINE_WIDTH)
            draw.line([(6, i), (6, i+2)], fill=COLOR_OUTLINE, width=LINE_WIDTH)
            draw.line([(26, i), (26, i+2)], fill=COLOR_OUTLINE, width=LINE_WIDTH)
           
    # ==========================================
    # NAVIGATION
    # ==========================================
    elif icon_name == "icon_left_arrow":
        draw_shape(draw, "polygon", [(22, 6), (10, 16), (22, 26)])
    elif icon_name == "icon_right_arrow":
        draw_shape(draw, "polygon", [(10, 6), (22, 16), (10, 26)])
    elif icon_name == "icon_up_arrow":
        draw_shape(draw, "polygon", [(6, 22), (16, 10), (26, 22)])
    elif icon_name == "icon_down_arrow":
        draw_shape(draw, "polygon", [(6, 10), (16, 22), (26, 10)])

    # ==========================================
    # ÉDITEUR TILESET / TILEMAP
    # ==========================================
    elif icon_name == "icon_grid":
        draw_shape(draw, "rectangle", [(4, 4), (28, 28)])
        draw_shape(draw, "line", [(16, 4), (16, 28)])
        draw_shape(draw, "line", [(4, 16), (28, 16)])
    elif icon_name == "icon_stamp":
        draw_shape(draw, "polygon", [(12, 4), (20, 4), (20, 14), (24, 18), (24, 26), (8, 26), (8, 18), (12, 14)])
        draw_shape(draw, "line", [(6, 28), (26, 28)])
    elif icon_name == "icon_eyedropper":
        draw_shape(draw, "polygon", [(22, 6), (28, 12), (14, 26), (8, 26), (8, 20)])
        draw_shape(draw, "line", [(8, 26), (4, 30)])
        draw_shape(draw, "line", [(18, 10), (24, 16)])
    elif icon_name == "icon_zoom_in":
        draw_shape(draw, "ellipse", [(6, 6), (20, 20)])
        draw_shape(draw, "line", [(18, 18), (28, 28)])
        draw_shape(draw, "line", [(10, 13), (16, 13)])
        draw_shape(draw, "line", [(13, 10), (13, 16)])
    elif icon_name == "icon_zoom_out":
        draw_shape(draw, "ellipse", [(6, 6), (20, 20)])
        draw_shape(draw, "line", [(18, 18), (28, 28)])
        draw_shape(draw, "line", [(10, 13), (16, 13)])
    elif icon_name == "icon_layer_add":
        draw_shape(draw, "polygon", [(4, 20), (14, 26), (24, 20), (14, 14)])
        draw_shape(draw, "polygon", [(4, 14), (14, 20), (24, 14), (14, 8)])
        draw_shape(draw, "line", [(24, 6), (30, 6)])
        draw_shape(draw, "line", [(27, 3), (27, 9)])
    elif icon_name == "icon_layer_remove":
        draw_shape(draw, "polygon", [(4, 20), (14, 26), (24, 20), (14, 14)])
        draw_shape(draw, "polygon", [(4, 14), (14, 20), (24, 14), (14, 8)])
        draw_shape(draw, "line", [(24, 6), (30, 6)])
    elif icon_name == "icon_visibility":
        draw_shape(draw, "polygon", [(4, 16), (16, 8), (28, 16), (16, 24)])
        draw_shape(draw, "ellipse", [(12, 12), (20, 20)])
    elif icon_name == "icon_flip_h":
        draw_shape(draw, "line", [(16, 4), (16, 28)])
        draw_shape(draw, "polygon", [(12, 10), (4, 16), (12, 22)])
        draw_shape(draw, "polygon", [(20, 10), (28, 16), (20, 22)])
    elif icon_name == "icon_flip_v":
        draw_shape(draw, "line", [(4, 16), (28, 16)])
        draw_shape(draw, "polygon", [(10, 12), (16, 4), (22, 12)])
        draw_shape(draw, "polygon", [(10, 20), (16, 28), (22, 20)])
    elif icon_name == "icon_rotate":
        draw.arc([(6, 6), (26, 26)], start=45, end=315, fill=COLOR_OUTLINE, width=LINE_WIDTH)
        draw_shape(draw, "polygon", [(22, 4), (28, 10), (20, 12)])
    elif icon_name == "icon_collision":
        draw_shape(draw, "rectangle", [(6, 6), (26, 26)])
        draw_shape(draw, "line", [(6, 6), (26, 26)])
        draw_shape(draw, "line", [(6, 26), (26, 6)])
    elif icon_name == "icon_settings":
        draw_shape(draw, "ellipse", [(8, 8), (24, 24)])
        draw_shape(draw, "ellipse", [(13, 13), (19, 19)])
        cogs = [[(14, 4), (18, 4), (18, 8), (14, 8)], [(14, 24), (18, 24), (18, 28), (14, 28)],
                [(4, 14), (8, 14), (8, 18), (4, 18)], [(24, 14), (28, 14), (28, 18), (24, 18)]]
        for cog in cogs:
            draw.polygon(cog, fill=COLOR_OUTLINE)
    elif icon_name == "icon_play":
        draw_shape(draw, "polygon", [(10, 6), (10, 26), (26, 16)])
    elif icon_name == "icon_magic_wand":
        draw_shape(draw, "polygon", [(22, 6), (26, 10), (12, 24), (8, 20)])
        draw.polygon([(24, 4), (28, 8), (22, 14), (18, 10)], fill=COLOR_OUTLINE)
        draw_shape(draw, "line", [(26, 2), (28, 4)])
        draw_shape(draw, "line", [(30, 8), (28, 10)])
        draw_shape(draw, "line", [(20, 2), (22, 4)])
        
    # ==========================================
    # PRESSE-PAPIERS
    # ==========================================
    elif icon_name == "icon_cut":
        draw_shape(draw, "ellipse", [(6, 20), (14, 28)])
        draw_shape(draw, "ellipse", [(18, 20), (26, 28)])
        draw_shape(draw, "line", [(12, 21), (22, 6)])
        draw_shape(draw, "line", [(20, 21), (10, 6)])
        draw.ellipse([(14, 14), (18, 18)], fill=COLOR_OUTLINE)
    elif icon_name == "icon_copy":
        draw_shape(draw, "polygon", [(6, 22), (6, 6), (20, 6), (20, 22)])
        draw_shape(draw, "polygon", [(12, 12), (26, 12), (26, 28), (12, 28)])
    elif icon_name == "icon_paste":
        draw_shape(draw, "rectangle", [(8, 6), (24, 28)])
        draw_shape(draw, "rectangle", [(12, 4), (20, 10)])
        draw_shape(draw, "rectangle", [(10, 12), (22, 26)])

    # ==========================================
    # FORMATAGE DE TEXTE
    # ==========================================
    elif icon_name == "icon_bold":
        draw_shape(draw, "line", [(10, 6), (10, 26)], width=3)
        draw.arc([(8, 6), (22, 16)], 270, 90, fill=COLOR_OUTLINE, width=3)
        draw.arc([(8, 16), (24, 26)], 270, 90, fill=COLOR_OUTLINE, width=3)
    elif icon_name == "icon_italic":
        draw_shape(draw, "line", [(18, 6), (14, 26)], width=3)
        draw_shape(draw, "line", [(14, 6), (22, 6)], width=2)
        draw_shape(draw, "line", [(10, 26), (18, 26)], width=2)
    elif icon_name == "icon_underline":
        draw_shape(draw, "line", [(10, 6), (10, 18)], width=2)
        draw_shape(draw, "line", [(22, 6), (22, 18)], width=2)
        draw.arc([(10, 10), (22, 24)], 0, 180, fill=COLOR_OUTLINE, width=2)
        draw_shape(draw, "line", [(6, 28), (26, 28)], width=3)
    elif icon_name == "icon_strikethrough":
        draw_shape(draw, "line", [(16, 4), (8, 26)], width=2)
        draw_shape(draw, "line", [(16, 4), (24, 26)], width=2)
        draw_shape(draw, "line", [(12, 18), (20, 18)], width=2)
        draw_shape(draw, "line", [(4, 14), (28, 14)], width=3)
    elif icon_name == "icon_text_color":
        draw_shape(draw, "line", [(16, 4), (8, 22)], width=2)
        draw_shape(draw, "line", [(16, 4), (24, 22)], width=2)
        draw_shape(draw, "line", [(12, 16), (20, 16)], width=2)
        draw.rectangle([(6, 26), (26, 30)], fill=COLOR_OUTLINE)
    elif icon_name == "icon_highlight":
        draw_shape(draw, "polygon", [(6, 22), (10, 26), (24, 12), (20, 8)])
        draw_shape(draw, "polygon", [(6, 22), (10, 26), (4, 28)])
        draw.rectangle([(4, 30), (28, 30)], fill=COLOR_FILL, outline=COLOR_OUTLINE, width=1)

    # ==========================================
    # ALIGNEMENT ET PARAGRAPHE
    # ==========================================
    elif icon_name == "icon_align_left":
        for l in [[(6,8), (24,8)], [(6,14), (18,14)], [(6,20), (26,20)], [(6,26), (20,26)]]: draw_shape(draw, "line", l)
    elif icon_name == "icon_align_center":
        for l in [[(8,8), (24,8)], [(12,14), (20,14)], [(6,20), (26,20)], [(10,26), (22,26)]]: draw_shape(draw, "line", l)
    elif icon_name == "icon_align_right":
        for l in [[(8,8), (26,8)], [(14,14), (26,14)], [(6,20), (26,20)], [(12,26), (26,26)]]: draw_shape(draw, "line", l)
    elif icon_name == "icon_align_justify":
        for l in [[(6,8), (26,8)], [(6,14), (26,14)], [(6,20), (26,20)], [(6,26), (26,26)]]: draw_shape(draw, "line", l)
    
    elif icon_name == "icon_bullet_list":
        for y in [8, 16, 24]:
            draw.ellipse([(6, y-2), (10, y+2)], fill=COLOR_OUTLINE)
            draw_shape(draw, "line", [(14, y), (26, y)])
    elif icon_name == "icon_number_list":
        draw_shape(draw, "line", [(8, 6), (8, 10)], width=1)
        draw_shape(draw, "line", [(6, 14), (10, 14), (6, 18), (10, 18)], width=1)
        draw_shape(draw, "line", [(6, 22), (10, 22), (10, 26), (6, 26)], width=1)
        draw_shape(draw, "line", [(6, 24), (10, 24)], width=1)
        for y in [8, 16, 24]: draw_shape(draw, "line", [(14, y), (26, y)])

    elif icon_name == "icon_indent_increase":
        draw_shape(draw, "polygon", [(4, 12), (10, 16), (4, 20)])
        for y in [8, 16, 24]: draw_shape(draw, "line", [(14, y), (28, y)])
    elif icon_name == "icon_indent_decrease":
        draw_shape(draw, "polygon", [(10, 12), (4, 16), (10, 20)])
        for y in [8, 16, 24]: draw_shape(draw, "line", [(14, y), (28, y)])

    # ==========================================
    # INSERTION ET RECHERCHE
    # ==========================================
    elif icon_name == "icon_insert_image":
        draw_shape(draw, "rectangle", [(4, 6), (28, 26)])
        draw_shape(draw, "polygon", [(4, 26), (12, 16), (20, 26)])
        draw_shape(draw, "polygon", [(16, 26), (22, 18), (28, 26)])
        draw.ellipse([(20, 8), (24, 12)], fill=COLOR_OUTLINE)
    elif icon_name == "icon_insert_table":
        draw_shape(draw, "rectangle", [(6, 6), (26, 26)])
        draw_shape(draw, "line", [(16, 6), (16, 26)])
        draw_shape(draw, "line", [(6, 13), (26, 13)])
        draw_shape(draw, "line", [(6, 20), (26, 20)])
    elif icon_name == "icon_insert_link":
        draw.arc([(6, 12), (18, 20)], start=45, end=315, fill=COLOR_OUTLINE, width=LINE_WIDTH)
        draw.arc([(14, 12), (26, 20)], start=225, end=135, fill=COLOR_OUTLINE, width=LINE_WIDTH)
        draw_shape(draw, "line", [(14, 18), (18, 14)])
    elif icon_name == "icon_find":
        for l in [[(4,8), (16,8)], [(4,14), (12,14)], [(4,20), (14,20)], [(4,26), (18,26)]]: draw_shape(draw, "line", l, width=1)
        draw_shape(draw, "ellipse", [(16, 12), (26, 22)])
        draw_shape(draw, "line", [(24, 20), (30, 26)], width=3)
        
    # ==========================================
    # GESTION DYNAMIQUE DES FICHIERS AVEC TEXTE
    # ==========================================
    elif icon_name == "icon_folder":
        # Dossier standard
        draw_shape(draw, "polygon", [(4, 8), (12, 8), (16, 12), (28, 12), (28, 26), (4, 26)])
        draw_shape(draw, "line", [(4, 14), (28, 14)])
        
    elif icon_name == "icon_file":
        # Fichier vierge
        draw_shape(draw, "polygon", [(6, 4), (18, 4), (26, 12), (26, 28), (6, 28)])
        draw_shape(draw, "polygon", [(18, 4), (18, 12), (26, 12)])
        
    elif icon_name.startswith("icon_file_"):
        # 1. Base du fichier
        draw_shape(draw, "polygon", [(6, 4), (18, 4), (26, 12), (26, 28), (6, 28)])
        draw_shape(draw, "polygon", [(18, 4), (18, 12), (26, 12)])
        
        # 2. Extraction du libellé
        ext = icon_name.replace("icon_file_", "").upper()
        
        # Mapping spécifique pour un meilleur rendu
        label_map = {
            "CODE": "</>",
            "IMAGE": "IMG",
            "VIDEO": "VID",
            "ARCHIVE": "ZIP",
            "TEXT": "TXT",
            "JSON": "JSN", # 4 lettres c'est parfois trop large
            "YAML": "YML",
            "MIDI": "MID"
        }
        text_to_draw = label_map.get(ext, ext)

        # 3. Écriture du texte centré (support compatible Pillow récent/ancien)
        try:
            draw.text((16, 20), text_to_draw, fill=(255, 255, 255, 255), font=FONT, anchor="mm", spacing=1)
        except AttributeError:
            # Fallback si ancienne version de PIL sans anchor="mm"
            draw.text((8, 15), text_to_draw[:3], fill=(255, 255, 255, 255), font=FONT, spacing=1)

    # ==========================================
    # LECTEUR MULTIMÉDIA (AUDIO/VIDÉO)
    # ==========================================
    elif icon_name == "icon_play":
        draw_shape(draw, "polygon", [(10, 6), (10, 26), (26, 16)])
    elif icon_name == "icon_pause":
        draw_shape(draw, "rectangle", [(8, 8), (14, 24)])
        draw_shape(draw, "rectangle", [(18, 8), (24, 24)])
    elif icon_name == "icon_stop":
        draw_shape(draw, "rectangle", [(8, 8), (24, 24)])
    elif icon_name == "icon_next":
        draw_shape(draw, "polygon", [(8, 8), (8, 24), (20, 16)])
        draw_shape(draw, "rectangle", [(20, 8), (24, 24)])
    elif icon_name == "icon_prev":
        draw_shape(draw, "polygon", [(24, 8), (24, 24), (12, 16)])
        draw_shape(draw, "rectangle", [(8, 8), (12, 24)])
    elif icon_name == "icon_fast_forward":
        draw_shape(draw, "polygon", [(4, 10), (4, 22), (16, 16)])
        draw_shape(draw, "polygon", [(16, 10), (16, 22), (28, 16)])
    elif icon_name == "icon_rewind":
        draw_shape(draw, "polygon", [(28, 10), (28, 22), (16, 16)])
        draw_shape(draw, "polygon", [(16, 10), (16, 22), (4, 16)])
    elif icon_name == "icon_volume_down":
        draw_shape(draw, "polygon", [(6, 14), (12, 14), (16, 8), (16, 24), (12, 18), (6, 18)])
        draw.arc([(12, 10), (20, 22)], 315, 45, fill=COLOR_OUTLINE, width=LINE_WIDTH) # 1 onde
    elif icon_name == "icon_volume_up":
        draw_shape(draw, "polygon", [(6, 14), (12, 14), (16, 8), (16, 24), (12, 18), (6, 18)])
        draw.arc([(12, 10), (20, 22)], 315, 45, fill=COLOR_OUTLINE, width=LINE_WIDTH) # Onde 1
        draw.arc([(8, 6), (24, 26)], 315, 45, fill=COLOR_OUTLINE, width=LINE_WIDTH)   # Onde 2
    elif icon_name == "icon_volume_mute":
        draw_shape(draw, "polygon", [(4, 14), (10, 14), (14, 8), (14, 24), (10, 18), (4, 18)])
        draw_shape(draw, "lines", [(18, 12), (26, 20)]) # Croix X
        draw_shape(draw, "lines", [(18, 20), (26, 12)])
    elif icon_name == "icon_shuffle":
        # Flèches croisées
        draw_shape(draw, "lines", [(4, 22), (10, 22), (22, 10), (28, 10)])
        draw_shape(draw, "lines", [(4, 10), (10, 10), (22, 22), (28, 22)])
        draw_shape(draw, "polygon", [(24, 6), (28, 10), (24, 14)])   # Pointe haut
        draw_shape(draw, "polygon", [(24, 18), (28, 22), (24, 26)])  # Pointe bas
    elif icon_name == "icon_repeat":
        # Flèches en cercle
        draw.arc([(6, 6), (26, 26)], 45, 315, fill=COLOR_OUTLINE, width=LINE_WIDTH)
        draw_shape(draw, "polygon", [(22, 10), (26, 2), (30, 10)]) # Pointe de flèche
    elif icon_name == "icon_fullscreen":
        # Coins externes
        draw_shape(draw, "lines", [(6, 12), (6, 6), (12, 6)])
        draw_shape(draw, "lines", [(20, 6), (26, 6), (26, 12)])
        draw_shape(draw, "lines", [(6, 20), (6, 26), (12, 26)])
        draw_shape(draw, "lines", [(26, 20), (26, 26), (20, 26)])
    elif icon_name == "icon_minimize":
        # Coins internes
        draw_shape(draw, "lines", [(6, 12), (12, 12), (12, 6)])
        draw_shape(draw, "lines", [(26, 12), (20, 12), (20, 6)])
        draw_shape(draw, "lines", [(6, 20), (12, 20), (12, 26)])
        draw_shape(draw, "lines", [(26, 20), (20, 20), (20, 26)])

    # ==========================================
    # MÉTÉO
    # ==========================================
    elif icon_name == "icon_weather_sun":
        draw_shape(draw, "ellipse", [(10, 10), (22, 22)])
        rays = [[(16, 2), (16, 6)], [(16, 26), (16, 30)], [(2, 16), (6, 16)], [(26, 16), (30, 16)],
                [(6, 6), (9, 9)], [(26, 26), (23, 23)], [(26, 6), (23, 9)], [(6, 26), (9, 23)]]
        for r in rays: draw_shape(draw, "line", r)
        
    elif icon_name == "icon_weather_cloud":
        draw_cloud(draw, offset_y=0)
        
    elif icon_name == "icon_weather_partly_cloudy":
        # Soleil en arrière-plan (dessiné en premier)
        draw_shape(draw, "ellipse", [(4, 4), (16, 16)])
        draw_shape(draw, "line", [(10, 0), (10, 4)])
        draw_shape(draw, "line", [(0, 10), (4, 10)])
        draw_shape(draw, "line", [(18, 2), (15, 5)])
        # Le nuage est dessiné par-dessus et masquera le bas du soleil !
        draw_cloud(draw, offset_y=2)
        
    elif icon_name == "icon_weather_rain":
        # On remonte le nuage de 4 pixels pour laisser de la place à la pluie
        draw_cloud(draw, offset_y=-4)
        draw_shape(draw, "line", [(10, 22), (8, 28)])
        draw_shape(draw, "line", [(16, 22), (14, 28)])
        draw_shape(draw, "line", [(22, 22), (20, 28)])
        
    elif icon_name == "icon_weather_storm":
        draw_cloud(draw, offset_y=-4)
        # Éclair
        draw_shape(draw, "polygon", [(16, 16), (12, 22), (16, 22), (14, 28), (22, 20), (18, 20)])
        
    elif icon_name == "icon_weather_snow":
        draw_shape(draw, "line", [(16, 4), (16, 28)])
        draw_shape(draw, "line", [(6, 10), (26, 22)])
        draw_shape(draw, "line", [(6, 22), (26, 10)])
        draw_shape(draw, "line", [(16, 4), (12, 8)])
        draw_shape(draw, "line", [(16, 4), (20, 8)])
        draw_shape(draw, "line", [(16, 28), (12, 24)])
        draw_shape(draw, "line", [(16, 28), (20, 24)])
        
    elif icon_name == "icon_weather_wind":
        draw_shape(draw, "lines", [(4, 12), (20, 12), (26, 8), (20, 4)])
        draw_shape(draw, "lines", [(8, 20), (28, 20), (24, 26), (18, 24)])
        draw_shape(draw, "line", [(2, 16), (12, 16)])
        
    elif icon_name == "icon_weather_thermometer":
        draw_shape(draw, "line", [(14, 8), (14, 20)])
        draw_shape(draw, "line", [(18, 8), (18, 20)])
        draw.arc([(14, 6), (18, 10)], 180, 360, fill=COLOR_OUTLINE, width=LINE_WIDTH)
        draw_shape(draw, "ellipse", [(12, 20), (20, 28)])
        draw_shape(draw, "line", [(16, 14), (16, 24)], width=2)
        for y in [10, 14, 18]: draw_shape(draw, "line", [(20, y), (24, y)], width=1)
            
    elif icon_name == "icon_weather_humidity":
        draw_shape(draw, "polygon", [(16, 4), (24, 16), (22, 26), (10, 26), (8, 16)])
        draw.arc([(12, 16), (20, 24)], 0, 90, fill=(255, 255, 255, 255), width=2)

    # Sauvegarde de l'image
    os.makedirs(PATH, exist_ok=True)
    filename = os.path.join(PATH, f"{icon_name}.png")
    img.save(filename)
    print(f"Généré : {filename}")

if __name__ == "__main__":
    print(f"Début de la génération des {len(ICONS)} icônes...")
    for icon in ICONS:
        generate_icon(icon)
    print(f"Terminé ! Toutes les icônes sont sauvegardées dans : {PATH}")