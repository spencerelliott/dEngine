#!/bin/zsh

# Clear out the assets folder
rm -rf disc/
mkdir disc/

# Copy all non-image assets to the disc folder
for ext in data/**/*.{cp,dat,map,mtl,obj,md5mesh,md5anim,scene,cfg,lbr};
do
  # Convert image assets
  for f in $ext;
  do
    # Get the relevant data for the current file
    NEW_PATH=$(grealpath --relative-to="${PWD}/data" "${f}")
    NEW_PATH="disc/data/${NEW_PATH}"
    BASE_NAME=$(basename -- "${f}")

    # Convert the paths and file names into what is needed
    DIR_PATH=$(dirname "${NEW_PATH}")
    FILE_NAME="${BASE_NAME%.*}"

    # Build the new folder paths, if needed
    mkdir -p "${DIR_PATH}"

    cp "${f}" "${DIR_PATH}/${BASE_NAME}"
  done
done

# Convert image assets
for f in data/**/*.png;
do
  # Get the relevant data for the current file
  NEW_PATH=$(grealpath --relative-to="${PWD}/data/textures" "${f}")
  NEW_PATH="disc/data/texturesPVR/${NEW_PATH}"
  BASE_NAME=$(basename -- "${f}")

  # Convert the paths and file names into what is needed
  DIR_PATH=$(dirname "${NEW_PATH}")
  FILE_NAME="${BASE_NAME%.*}"

  # Build the new folder paths, if needed
  mkdir -p "${DIR_PATH}"

  echo "Converting ${f} (${FILE_NAME}) to PVR in ${DIR_PATH}..."

  # Convert and output the textures (enable compression later)
  $KOS_BASE/utils/texconv/texconv -f ARGB4444 -i "${f}" -o "${DIR_PATH}/${BASE_NAME}.argb4444.pvr"
done
