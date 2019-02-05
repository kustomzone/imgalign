
import { ImageDataConversion } from '@/utilities/ImageDataConversion';
import { paramTypes } from '@/models/constants/params';
import { ExifHelper } from '@/utilities/ExifHelper';

const state = {
  imageDataArray: [],
  imageDataUrlsArray: [],
  imageFileArray: [],
  imageFieldOfViewArray: [],
  imageFieldOfViewInitialArray: [],
  busy: false,
  busyLoading: false,
  indicesSelected: []
}

const getters = {
  imageDataArray(state) {
    return state.imageDataArray;
  },
  imageDataUrlsArray(state) {
    return state.imageDataUrlsArray;
  },
  imageFileArray(state) {
    return state.imageFileArray;
  },
  imageFieldOfViewArray(state) {
    return state.imageFieldOfViewArray;
  },
  busy(state) {
    return state.busy;
  },
  busyLoading(state) {
    return state.busyLoading;
  },
  indicesSelected(state) {
    return state.indicesSelected;
  },
  imageFieldOfView(state) {
    return index => state.imageFieldOfViewArray[index];
  },
  imageFieldOfViewInitial(state) {
    return index => state.imageFieldOfViewInitialArray[index];
  },
  imageDataValid(state) {
    return index => {
      return ((state.imageDataArray.length > index
        && state.imageDataArray[index]
        && state.imageDataArray[index].width > 0
        && state.imageDataArray[index].height > 0) 
      ||  (state.imageFileArray.length > index 
        && state.imageFileArray[index]));
    }
  },
  imageCount(state) {
    return state.imageDataArray.length;
  }
}

const mutations = {

  imageDataArray(state, imageDataArray) {
    state.imageDataArray = imageDataArray;
  },
  imageData(state, imageData) {
    state.imageDataArray.push(imageData);
  },
  _imageDataUrl(state, imageDataUrl) {
    state.imageDataUrlsArray.push(imageDataUrl);
  },
  _imageFile(state, file) {
    state.imageFileArray.push(file);
  },
  imageFieldOfView(state, fieldOfView) {
    state.imageFieldOfViewArray.push(fieldOfView);
  },
  imageFieldOfViewInitial(state, fieldOfView) {
    state.imageFieldOfViewInitialArray.push(fieldOfView);
  },
  imageDataUrlsArray(state, imageDataUrlsArray) {
    state.imageDataUrlsArray = imageDataUrlsArray;
  },
  imageFieldOfViewUpdate(state, { index, fieldOfView }) {
    state.imageFieldOfViewArray.splice(index, 1, fieldOfView);
  },
  imageClicked(state, value) {
    if(value == -1) {
      state.indicesSelected = [];
      return;
    }
    const index = state.indicesSelected.findIndex(selectedIndex => selectedIndex == value);
    if(index == -1) {
      state.indicesSelected.push(value);
    }
    else {
      state.indicesSelected.splice(index, 1);
    }
  },
  busy(state, value) {
    state.busy = value;
  },
  busyLoading(state, value) {
    state.busyLoading = value;
  },
  swap(state, { indexFrom, indexTo} ) {
    const tempImageData = state.imageDataArray[indexTo];
    const tempImageDataUrl = state.imageDataUrlsArray[indexTo];
    const tempImageFile = state.imageFileArray[indexTo];
    const tempFieldOfView = state.imageFieldOfViewArray[indexTo];
    const tempFieldOfViewInitial = state.imageFieldOfViewInitialArray[indexTo];

    state.imageDataArray.splice(indexTo, 1, state.imageDataArray[indexFrom]);
    state.imageDataUrlsArray.splice(indexTo, 1, state.imageDataUrlsArray[indexFrom]);
    state.imageFileArray.splice(indexTo, 1, state.imageFileArray[indexFrom]);
    state.imageFieldOfViewArray.splice(indexTo, 1, state.imageFieldOfViewArray[indexFrom]);
    state.imageFieldOfViewInitialArray.splice(indexTo, 1, state.imageFieldOfViewInitialArray[indexFrom]);

    state.imageDataArray.splice(indexFrom, 1, tempImageData);
    state.imageDataUrlsArray.splice(indexFrom, 1, tempImageDataUrl);
    state.imageFileArray.splice(indexFrom, 1, tempImageFile);
    state.imageFieldOfViewArray.splice(indexFrom, 1, tempFieldOfView);
    state.imageFieldOfViewInitialArray.splice(indexFrom, 1, tempFieldOfViewInitial);
  },
  removeSelected(state) {

    state.imageDataArray = state.imageDataArray.filter((_, index) => !state.indicesSelected.some(val => val == index));
    state.imageDataUrlsArray = state.imageDataUrlsArray.filter((_, index) => !state.indicesSelected.some(val => val == index));
    state.imageFileArray = state.imageFileArray.filter((_, index) => !state.indicesSelected.some(val => val == index));
    state.imageFieldOfViewArray = state.imageFieldOfViewArray.filter((_, index) => !state.indicesSelected.some(val => val == index));
    state.imageFieldOfViewInitialArray = state.imageFieldOfViewInitialArray.filter((_, index) => !state.indicesSelected.some(val => val == index));

    state.indicesSelected = [];
  },
  removeAll(state) {
    state.imageDataArray = [];
    state.imageDataUrlsArray = [];
    state.imageFileArray = [];
    state.imageFieldOfViewArray = [];
    state.imageFieldOfViewInitialArray = [];

    state.indicesSelected = [];
  },
  freeImageData(state) {
    const count = state.imageDataArray.length;
    state.imageDataArray = [];
    state.imageDataArray = new Array(count).fill(null);
  }
}

const actions = {

  async imageData({ commit, dispatch, rootGetters }, imageData) {
    
    if(imageData && rootGetters['worker/ready']) {
      const maxPixelsN = rootGetters['settings/param'](paramTypes.imageCapInput.id);
      if(imageData.width * imageData.height > maxPixelsN) {
        const scaleF = Math.sqrt(maxPixelsN / (imageData.width * imageData.height));
        const height = Math.floor(imageData.height * scaleF);
        const width = Math.floor(imageData.width * scaleF);
        if(rootGetters['worker/ready']) {
          await dispatch('worker/setMultiInputImageResized', {
              imageDataSrc: imageData,
              width,
              height
            }, { root: true });
        }
        return;
      }
    }

    commit('imageData', imageData);
    await dispatch('imageDataUrl', imageData);

    // if(rootGetters['worker/ready']) {
    //   dispatch('worker/multiStitchResetWorkerData', null, { root: true });
    //   commit('worker/results/imageData', { name: multiStitchName, imageData: null}, { root: true });
    // }
  },

  async imageDataUrl({ commit, dispatch, rootGetters }, imageData) {
    
    if(!imageData) {
      commit('_imageDataUrl', null);
      return;
    }

    const maxPixelsN = rootGetters['settings/param'](paramTypes.multiStitch_limitInputView.id);
    if(imageData.width * imageData.height > maxPixelsN) {
      const scaleF = Math.sqrt(maxPixelsN / (imageData.width * imageData.height));
      const height = Math.floor(imageData.height * scaleF);
      const width = Math.floor(imageData.width * scaleF);
      
      if(rootGetters['worker/ready']) {
        await dispatch('worker/setMultiInputImageUrlResized', {
            imageDataSrc: imageData,
            width,
            height
          },
          { root: true });
      }
      return;
    }

    if(rootGetters['worker/ready']) {
      commit('_imageDataUrl', ImageDataConversion.imageSrcFromImageData(imageData));
    }
  },
  async imageFile(context, file) {

    const wasBusy = context.getters['busy'];

    return new Promise((resolve) => {

      const img = new Image(); 
      img.onload = async () => {
        try {
          const imageData = ImageDataConversion.imageDataFromImageSrc(img);
          await context.dispatch('imageData', imageData);
          context.commit('_imageFile', file);
          
          const fieldOfView = await ExifHelper.getFieldOfViewAsync(img);
          if(fieldOfView) {
            context.commit('imageFieldOfView', fieldOfView);
            context.commit('imageFieldOfViewInitial', fieldOfView);
          }
          else {
            context.commit('imageFieldOfView', 45);
            context.commit('imageFieldOfViewInitial', 45);
          }

          img.onload = null;
        }
        finally {
          if(!wasBusy) context.commit('busy', false);
          resolve();
        }
      }
      if(file) {
        if(!wasBusy) context.commit('busy', true);
        img.src = URL.createObjectURL(file);
      }
    });
  },
  async imageFiles(context, files) {
    try {
      context.commit('busy', true);

      const localFiles = []
      for(const file of files) {
        localFiles.push(file);
      }
      for(const file of localFiles) {
        await context.dispatch('imageFile', file);
      }
    }
    finally {
      context.commit('busy', false);
    }
  },
  removeAllOrSelected(context) {
    if(context.getters['indicesSelected'].length == 0) {
      context.commit('removeAll');
    }
    else {
      context.commit('removeSelected');
    }
  },

  async reloadFilesFromDisc(context) {
    const files = context.getters['imageFileArray'];
    context.commit('removeAll');
    await context.dispatch('imageFiles', files);
  },
  async reloadFilesFromDiscIf(context) {
    
    if(context.getters['imageFileArray'].length == 0) {
      return;
    }

    if(  context.getters['imageDataArray'].length == 0
      || context.getters['imageDataArray'][0] == null ) {
      
      await context.dispatch('reloadFilesFromDisc');
    }
  }

}

export default {
  namespaced: true,
  state,
  getters,
  mutations,
  actions
};