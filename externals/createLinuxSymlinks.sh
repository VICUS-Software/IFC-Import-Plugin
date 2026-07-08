#!/bin/bash

# Array mit den Ordnernamen
folders=("IBK" "IBKMK" "QtExt" "TiCPP" "clipper" "lib_x64" "Vicus" "Nandrad" "CCM" "DataIO" "glm" "VicOSM" "VicIFC" "QuaZIP" "HiGHS" "NandradFMUGenerator")

# Zielordner festlegen, in dem der Symlink erstellt werden soll
target_folder="../externals"
source_folder="../SIM-VICUS/externals"

for folder in "${folders[@]}"
do
  # Prüfen, ob der Ordner bereits existiert
  if [ -L "$target_folder/$folder" ]
  then
    read -p "Der Symlink für den Ordner $folder existiert bereits. Möchten Sie den Ordner löschen und einen neuen Symlink erstellen? (j/n)" choice
    case "$choice" in
      j|J )
        rm "$target_folder/$folder"
        ln -s "$source_folder/$folder" "$target_folder/$folder"
        echo "Der Symlink für den Ordner $folder wurde erfolgreich aktualisiert."
        ;;
      n|N )
        echo "Der bestehende Symlink für den Ordner $folder wurde beibehalten."
        ;;
      * )
        echo "Ungültige Eingabe. Der bestehende Symlink für den Ordner $folder wurde beibehalten."
        ;;
    esac
  else
    # Pfad zum ursprünglichen Ordner oder Datei
    source_path="$source_folder/$folder"

    # Symlink erstellen
    ln -s "$source_path" "$target_folder"
    echo "Der Symlink für den Ordner $folder wurde erfolgreich erstellt."
  fi
done

# NandradCodeGenerator symlink (lives at repo root, not in externals)
if [ -L "../NandradCodeGenerator" ]
then
  read -p "Der Symlink für NandradCodeGenerator existiert bereits. Möchten Sie den Ordner löschen und einen neuen Symlink erstellen? (j/n)" choice
  case "$choice" in
    j|J )
      rm "../NandradCodeGenerator"
      ln -s "SIM-VICUS/NandradCodeGenerator" "../NandradCodeGenerator"
      echo "Der Symlink für NandradCodeGenerator wurde erfolgreich aktualisiert."
      ;;
    n|N )
      echo "Der bestehende Symlink für NandradCodeGenerator wurde beibehalten."
      ;;
    * )
      echo "Ungültige Eingabe. Der bestehende Symlink für NandradCodeGenerator wurde beibehalten."
      ;;
  esac
else
  ln -s "SIM-VICUS/NandradCodeGenerator" "../NandradCodeGenerator"
  echo "Der Symlink für NandradCodeGenerator wurde erfolgreich erstellt."
fi

