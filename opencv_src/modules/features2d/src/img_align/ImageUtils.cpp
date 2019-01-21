

#include "ImageUtils.h"
#include "../precomp.hpp"
#include <opencv2/imgproc.hpp>
#include "warpers/warpers.hpp"
#include "opencv2/imgproc/types_c.h"
#include "LogUtils.h"
#include <math.h>
#include <limits>

#include "blenders/blenders.hpp"
#include "seamfinders/seam_finders.hpp"
#include "exposure_compensator/exposure_compensate.hpp"

namespace imgalign
{

inline cv::Size getSize(
  const std::vector<cv::Point> &tlCorners,
  const std::vector<cv::Size> &sizes)
{
  //CV_ASSERT(tlCorners.size() == sizes.size());

  cv::Point tl(std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
  cv::Point br(std::numeric_limits<int>::min(), std::numeric_limits<int>::min());
  for (size_t i = 0; i < tlCorners.size(); ++i)
  {
    tl.x = std::min(tl.x, tlCorners[i].x);
    tl.y = std::min(tl.y, tlCorners[i].y);
    br.x = std::max(br.x, tlCorners[i].x + sizes[i].width);
    br.y = std::max(br.y, tlCorners[i].y + sizes[i].height);
  }
  
  return cv::Size(br.x - tl.x, br.y - tl.y);
}

inline double getBlendWidth(
  double blendStrength,
  const std::vector<cv::Point> &tlCorners,
  const std::vector<cv::Size> &sizes)
{  
  auto fullSize = getSize(tlCorners, sizes);
  double area = fullSize.width * fullSize.height;
  return sqrt(area) * blendStrength / 100.f;
}

inline uint8_t divideBy255(int value) {
  return (uint8_t)((value + 1 + (value >> 8)) >> 8);
}

inline void blend(
  const cv::Vec4b &src1, const cv::Vec4b &src2,
  double src1W, double src2W, cv::Vec4b &dst) {

  dst[0] = src2W * src2[0] + src1W * src1[0];
  dst[1] = src2W * src2[1] + src1W * src1[1];
  dst[2] = src2W * src2[2] + src1W * src1[2];
  dst[3] = src2W * src2[3] + src1W * src1[3];
}

inline void blendI(
  const cv::Vec4b &src1, const cv::Vec4b &src2,
  int src1W, int src2W, cv::Vec4b &dst) {

  dst[0] = divideBy255(src2W * src2[0]) + divideBy255(src1W * src1[0]);
  dst[1] = divideBy255(src2W * src2[1]) + divideBy255(src1W * src1[1]);
  dst[2] = divideBy255(src2W * src2[2]) + divideBy255(src1W * src1[2]);
  dst[3] = divideBy255(src2W * src2[3]) + divideBy255(src1W * src1[3]);  
}

inline void blend(const cv::Vec4b &src, double srcW, cv::Vec4b &dst) {

  dst[0] = srcW * src[0];
  dst[1] = srcW * src[1];
  dst[2] = srcW * src[2];
  dst[3] = srcW * src[3];
}

inline void blend(
  const TMat &src1, const TMat &src2,
  int x, int y, double sqrDistDiff, int blendDistPx, TMat &dst)
{ 


  int blendDistPxSqr = blendDistPx * blendDistPx;
  
  if(sqrDistDiff < -blendDistPxSqr) {
    dst.at<cv::Vec4b>(y, x) = src1.at<cv::Vec4b>(y, x);
  }
  else if(sqrDistDiff > blendDistPxSqr) {
    dst.at<cv::Vec4b>(y, x) = src2.at<cv::Vec4b>(y, x);
  }
  else {
  
    double alphaSrc1, alphaSrc2;
    double alpha = 0.5 * sqrDistDiff / blendDistPxSqr;
    if(sqrDistDiff <= 0) {
      alphaSrc1 = 0.5 + -alpha;
      alphaSrc2 = 1.0 - alphaSrc1;
    }
    else {
      alphaSrc2 = 0.5 + alpha;
      alphaSrc1 = 1.0 - alphaSrc2;
    }

    blend(
      src1.at<cv::Vec4b>(y, x),
      src2.at<cv::Vec4b>(y,x),
      alphaSrc1,
      alphaSrc2,
      dst.at<cv::Vec4b>(y, x));
  }
}

cv::Size ImageUtils::bbox(
  const std::vector<TMat> &images,
  const std::vector<cv::Point> &tlCorners) 
  {
  int r, b;
  r = b = -std::numeric_limits<int>::max();

  for(size_t i = 0; i < images.size(); ++i) {
    auto r_ = images[i].size().width + tlCorners[i].x;
    if(r_ > r) r = r_;
    auto b_ = images[i].size().height + tlCorners[i].y;
    if(b_ > b) b = b_;
  }
  return cv::Size(r, b);
}

inline void blendI(const cv::Vec4b &src, int srcW, cv::Vec4b &dst) {

  dst[0] = divideBy255(srcW * src[0]);
  dst[1] = divideBy255(srcW * src[1]);
  dst[2] = divideBy255(srcW * src[2]);
  dst[3] = divideBy255(srcW * src[3]);
}

inline void blend(
  cv::detail::Blender &blender,
  const std::vector<TMat> &images,
	const std::vector<TMat> &masks,
	const std::vector<cv::Point> &tlCorners,
	TMat &outImage)
{
  FUNCLOGTIMEL("ImageUtils::blend");

  auto box = ImageUtils::bbox(images, tlCorners);
  blender.prepare(cv::Rect(0, 0, box.width, box.height));

  std::vector<TMat> images_16S3C(images.size());
  for(size_t i = 0; i < images.size(); ++i) {
    
    cv::cvtColor(images[i], images_16S3C[i], CV_RGBA2RGB);
    images_16S3C[i].convertTo(images_16S3C[i], CV_16S);

    blender.feed(images_16S3C[i], masks[i], tlCorners[i]);
  }

  TMat dst, dstMask;
  blender.blend(dst, dstMask);

  dst.convertTo(dst, CV_8U);
  TMat dst_rgba;
  cv::cvtColor(dst, dst_rgba, CV_RGB2RGBA);

  outImage = TMat::zeros(dst_rgba.size(), dst_rgba.type());
  dst_rgba.copyTo(outImage, dstMask);

  for(size_t i = 0; i < images.size(); ++i) {

    auto xAdd = tlCorners[i].x;
    auto yAdd = tlCorners[i].y;

    for(int y = 0; y < images[i].rows; ++y) {
      for(int x = 0; x < images[i].cols; ++x) {

        int y_ = y + yAdd;
        int x_ = x + xAdd;

        if(outImage.at<cv::Vec4b>(y_, x_)[3] != 0) {
          continue;
        }

        if(images[i].at<cv::Vec4b>(y, x)[3] < 255 && images[i].at<cv::Vec4b>(y, x)[3] > 0) {
          outImage.at<cv::Vec4b>(y_, x_)[0] = images[i].at<cv::Vec4b>(y, x)[0];
          outImage.at<cv::Vec4b>(y_, x_)[1] = images[i].at<cv::Vec4b>(y, x)[1];
          outImage.at<cv::Vec4b>(y_, x_)[2] = images[i].at<cv::Vec4b>(y, x)[2];
          outImage.at<cv::Vec4b>(y_, x_)[3] = images[i].at<cv::Vec4b>(y, x)[3];
        }
      }
    }
  }
}

void ImageUtils::blendImages(
  TConstMat& imageSrc,
  TConstMat& imageDst,
  double blendValueSrc,
  int doOverlay,
  TConstPoints& polygonMask,
  TMat& blendedMat)
{
  FUNCLOGTIMEL("ImageUtils::blendImages");

  cv::Mat mask = cv::Mat::zeros(imageDst.size(), CV_8UC1);
	if(polygonMask.size() > 0) {
		auto *pts = &polygonMask[0];
		int ptsN = (int)polygonMask.size();
		fillPoly(mask, &pts, &ptsN, 1, cv::Scalar::all(255), cv::LINE_AA);
	}

  blendImages(imageSrc, imageDst, blendValueSrc, doOverlay, mask, blendedMat);
}

void ImageUtils::blendImages(
  TConstMat& imageSrc,
  TConstMat& imageDst,
  double blendValueSrc,
  int doOverlay,
  TMat mask,
  TMat& blendedMat)
{
  FUNCLOGTIMEL("ImageUtils::blendImages");

  CV_Assert(imageSrc.size() == imageDst.size());
  CV_Assert(imageSrc.type() == imageDst.type());
  CV_Assert(blendedMat.size() == imageDst.size());
  CV_Assert(blendedMat.type() == imageDst.type());
  CV_Assert(mask.size() == imageDst.size());
  CV_Assert(imageSrc.channels() == 4);

  int blendValueSrcI = (int)(blendValueSrc * 255);
  auto blendValueDst = 1.0 - blendValueSrc; 
  int blendValueDstI = 255 - blendValueSrcI;
  //auto _blendValueSrc = blendValueSrc;
  //auto _blendValueDst = blendValueDst;
  int _blendValueSrcI = blendValueSrcI;
  int _blendValueDstI = blendValueDstI;

  cv::MatConstIterator_<cv::Vec4b> itSrc = imageSrc.begin<cv::Vec4b>();
  cv::MatConstIterator_<cv::Vec4b> itDst = imageDst.begin<cv::Vec4b>();
  cv::MatIterator_<cv::Vec4b> itBlend = blendedMat.begin<cv::Vec4b>();
  cv::MatConstIterator_<uint8_t> itMask = mask.begin<uint8_t>();
  
  if(!doOverlay) {

    for(; itSrc != imageSrc.end<cv::Vec4b>(); ++itSrc, ++itDst, ++itMask, ++itBlend) {

      if(*itMask == 0) {

        blend(*itDst, blendValueDst, *itBlend);
        //blendI(*itDst, blendValueDstI, *itBlend);
      }
      else if(*itMask == 255) {
        blend(*itSrc, *itDst, blendValueSrc, blendValueDst, *itBlend);
        //blendI(*itSrc, *itDst, blendValueSrcI, blendValueDstI, *itBlend);
      }
      else {
        //_blendValueSrc = blendValueSrc * *itMask / 255.0;
        _blendValueSrcI = divideBy255(*itMask * blendValueSrcI);

        //blend(*itSrc, *itDst, _blendValueSrc, blendValueDst, *itBlend);
        blendI(*itSrc, *itDst, _blendValueSrcI, blendValueDstI, *itBlend);
      }
    }
    return;
  }  

  for(; itSrc != imageSrc.end<cv::Vec4b>(); ++itSrc, ++itDst, ++itMask, ++itBlend) {
    if(*itMask == 0) {
      (*itBlend)[0] = (*itDst)[0];
      (*itBlend)[1] = (*itDst)[1];
      (*itBlend)[2] = (*itDst)[2];
      (*itBlend)[3] = (*itDst)[3];
    }
    else if(*itMask == 255) {

      if((*itSrc)[3] != 255) {

        //_blendValueSrc = blendValueSrc * (*itSrc)[3] / 255.0;
        _blendValueSrcI = divideBy255(blendValueSrcI * (*itSrc)[3]);

        //_blendValueDst = 1.0 - _blendValueSrc;
        _blendValueDstI = 255 - _blendValueSrcI;

        //blend(*itSrc, *itDst, _blendValueSrc, _blendValueDst, *itBlend);
        blendI(*itSrc, *itDst, _blendValueSrcI, _blendValueDstI, *itBlend);
      }
      else {
        blend(*itSrc, *itDst, blendValueSrc, blendValueDst, *itBlend);
        //blendI(*itSrc, *itDst, blendValueSrcI, blendValueDstI, *itBlend);
      }
    }
    else {
      //_blendValueSrc = blendValueSrc * *itMask / 255.0;
      _blendValueSrcI = divideBy255(blendValueSrcI * *itMask);

      if((*itSrc)[3] != 255) {
        //_blendValueSrc = _blendValueSrc * (*itSrc)[3] / 255;
        _blendValueSrcI = divideBy255(_blendValueSrcI * (*itSrc)[3]);

        //_blendValueDst = 1.0 - _blendValueSrc;
        _blendValueDstI = 255 - _blendValueSrcI;

        //blend(*itSrc, *itDst, _blendValueSrc, _blendValueDst, *itBlend);
        blendI(*itSrc, *itDst, _blendValueSrcI, _blendValueDstI, *itBlend);
      }
      else {
        //_blendValueDst = 1.0 - _blendValueSrc;
        _blendValueDstI = 255 - _blendValueSrcI;

        //blend(*itSrc, *itDst, _blendValueSrc, _blendValueDst, *itBlend);
        blendI(*itSrc, *itDst, _blendValueSrcI, _blendValueDstI, *itBlend);
      }
    }
  }
}

void ImageUtils::blendImages(
  TConstMat& imageSrc,
  TConstMat& imageDst,
  double blendValueSrc,
  int doOverlay,
  TMat& blendedMat)
{
  FUNCLOGTIMEL("ImageUtils::blendImages");

  CV_Assert(imageSrc.size() == imageDst.size());
  CV_Assert(imageSrc.type() == imageDst.type());
  CV_Assert(blendedMat.size() == imageDst.size());
  CV_Assert(blendedMat.type() == imageDst.type());
  CV_Assert(imageSrc.channels() == 4);

  cv::MatConstIterator_<cv::Vec4b> itSrc = imageSrc.begin<cv::Vec4b>();
  cv::MatConstIterator_<cv::Vec4b> itDst = imageDst.begin<cv::Vec4b>();
  cv::MatIterator_<cv::Vec4b> itBlend = blendedMat.begin<cv::Vec4b>();
  
  int blendValueSrcI = (int)255 * blendValueSrc;
  auto blendValueDst = 1.0 - blendValueSrc;
  auto blendValueDstI = 255 - blendValueSrcI;
  
  if(doOverlay) {

    //auto _blendValueSrc = blendValueSrc;
    //auto _blendValueDst = blendValueDst;
    auto _blendValueSrcI = blendValueSrcI;
    auto _blendValueDstI = blendValueDstI;

    for(; itSrc != imageSrc.end<cv::Vec4b>(); ++itSrc, ++itDst, ++itBlend) {

      if((*itSrc)[3] != 255) {
        //_blendValueSrc = blendValueSrc * (*itSrc)[3] / 255;
        //_blendValueDst = 1.0 - _blendValueSrc;

        _blendValueSrcI = divideBy255(blendValueSrcI * (*itSrc)[3]);
        _blendValueDstI = 255 - _blendValueSrcI;

        //blend(*itSrc, *itDst, _blendValueSrc, _blendValueDst, *itBlend);
        blendI(*itSrc, *itDst, _blendValueSrcI, _blendValueDstI, *itBlend);
      }
      else {

        blend(*itSrc, *itDst, blendValueSrc, blendValueDst, *itBlend);
        //blendI(*itSrc, *itDst, blendValueSrcI, blendValueDstI, *itBlend);
      }
    }
  }
  else {
    for(; itSrc != imageSrc.end<cv::Vec4b>(); ++itSrc, ++itDst, ++itBlend) {

      blend(*itSrc, *itDst, blendValueSrc, blendValueDst, *itBlend);
      //blendI(*itSrc, *itDst, blendValueSrcI, blendValueDstI, *itBlend);
    }
  }
}

void ImageUtils::blendImagesAlphaWhereOverlap(
  TConstMat& imageSrc,
  TConstMat& imageDst,
  double blendValueSrc,
  TMat& blendedMat)
{
  FUNCLOGTIMEL("ImageUtils::blendImagesAlphaWhereOverlap");

  CV_Assert(imageSrc.size() == imageDst.size());
  CV_Assert(imageSrc.type() == imageDst.type());
  CV_Assert(blendedMat.size() == imageDst.size());
  CV_Assert(blendedMat.type() == imageDst.type());
  CV_Assert(imageSrc.channels() == 4);

  cv::MatConstIterator_<cv::Vec4b> itSrc = imageSrc.begin<cv::Vec4b>();
  cv::MatConstIterator_<cv::Vec4b> itDst = imageDst.begin<cv::Vec4b>();
  cv::MatIterator_<cv::Vec4b> itBlend = blendedMat.begin<cv::Vec4b>();
  
  int blendValueSrcI = (int)255 * blendValueSrc;
  auto blendValueDst = 1.0 - blendValueSrc;
  auto blendValueDstI = 255 - blendValueSrcI;

  //auto _blendValueSrc = blendValueSrc;
  //auto _blendValueDst = blendValueDst;
  auto _blendValueSrcI = blendValueSrcI;
  auto _blendValueDstI = blendValueDstI;

  for(; itSrc != imageSrc.end<cv::Vec4b>(); ++itSrc, ++itDst, ++itBlend) {

    if((*itSrc)[3] == 255 && (*itDst)[3] == 255) {
      blend(*itSrc, *itDst, blendValueSrc, blendValueDst, *itBlend);
    }
    else if((*itSrc)[3] == 255) {
      if((*itDst)[3] == 0) {
        *itBlend = *itSrc;
      }
      else {
        _blendValueDstI = divideBy255(blendValueDstI * (*itDst)[3]);
        _blendValueSrcI = 255 - _blendValueDstI;

        blendI(*itSrc, *itDst, _blendValueSrcI, _blendValueDstI, *itBlend);
      }
    }
    else if((*itDst)[3] == 255) {
      if((*itSrc)[3] == 0) {
        *itBlend = *itDst;
      }
      else {
        _blendValueSrcI = divideBy255(blendValueSrcI * (*itSrc)[3]);
        _blendValueDstI = 255 - _blendValueSrcI;

        blendI(*itSrc, *itDst, _blendValueSrcI, _blendValueDstI, *itBlend);
      }
    }
    else if((*itDst)[3] != 0 || (*itSrc)[3] != 0){

      //_blendValueSrc = blendValueSrc * (*itSrc)[3] / 255;
      //_blendValueDst = blendValueDst * (*itDst)[3] / 255;

      _blendValueSrcI = divideBy255(blendValueSrcI * (*itSrc)[3]);
      _blendValueDstI = divideBy255(blendValueDstI * (*itDst)[3]);

      //blend(*itSrc, *itDst, _blendValueSrc, _blendValueDst, *itBlend);
      blendI(*itSrc, *itDst, _blendValueSrcI, _blendValueDstI, *itBlend);
    }
    else {
      (*itBlend)[0] = (*itBlend)[1] = (*itBlend)[2] = (*itBlend)[3] = 0;
    }
  }
}

void ImageUtils::dimImage(
  TConstMat& imageSrc,
  double dimValue,
  TMat& dimmedMat)
{
  FUNCLOGTIMEL("ImageUtils::dimImage");

  cv::MatConstIterator_<cv::Vec4b> itSrc = imageSrc.begin<cv::Vec4b>();
  cv::MatIterator_<cv::Vec4b> itBlend = dimmedMat.begin<cv::Vec4b>();
  for(; itSrc != imageSrc.end<cv::Vec4b>(); ++itSrc, ++itBlend) {
    blend(*itSrc, 1.0 - dimValue, *itBlend);
  }
}

void ImageUtils::clearPolygonInvFromImage(
  TConstMat& imageSrc,
  TConstPoints& polygonMask,
  TMat& outPolygonImage,
  TMat& outPolygonMask)
{
  FUNCLOGTIMEL("ImageUtils::clearPolygonInvFromImage");

  outPolygonMask = cv::Mat::zeros(imageSrc.size(), CV_8UC1);
  outPolygonImage = cv::Mat::zeros(imageSrc.size(), imageSrc.type());

	if(polygonMask.size() > 0) {
		auto *pts = &polygonMask[0];
		int ptsN = (int)polygonMask.size();
		fillPoly(outPolygonMask, &pts, &ptsN, 1, cv::Scalar::all(255), cv::LINE_AA);
	}

  imageSrc.copyTo(outPolygonImage, outPolygonMask);
}

void ImageUtils::floodFillMask(
  TConstMat& imageSrc,
  cv::Point ptSeed,
  int floodFillTolerance,
  TMat& outMask)
{
  FUNCLOGTIMEL("ImageUtils::floodFillMask");
  
  if( ptSeed.x >= imageSrc.size().width
   || ptSeed.y >= imageSrc.size().height
   || ptSeed.x < 0
   || ptSeed.y < 0) {

     outMask = cv::Mat::zeros(imageSrc.size(), CV_8UC1);
     return;
  }
  CV_Assert(imageSrc.channels() == 4);

  cv::Mat imageSrcRgb;
  cvtColor(imageSrc, imageSrcRgb, CV_RGBA2RGB);

  cv::Mat mask = cv::Mat::zeros(
    cv::Size(
      imageSrcRgb.size().width + 2,
      imageSrcRgb.size().height + 2),
    CV_8UC1);

  floodFill(
    imageSrcRgb,
    mask,
    ptSeed,
    cv::Scalar::all(255),
    nullptr,
    cv::Scalar::all(floodFillTolerance),
    cv::Scalar::all(floodFillTolerance),
    8 | (255 << 8) | CV_FLOODFILL_MASK_ONLY);

    cv::Rect roi(1, 1, mask.size().width - 2, mask.size().height - 2);
    outMask = mask(roi);
}

void ImageUtils::floodFillMask(
  TConstMat& imageSrc,
  std::vector<cv::Point> ptsSeed,
  std::vector<int> &floodFillTolerances,
  TMat& outMask)
{
  FUNCLOGTIMEL("ImageUtils::floodFillMask");

  CV_Assert(ptsSeed.size() == floodFillTolerances.size());

  std::vector<TMat> masks(ptsSeed.size());
  std::vector<TMat>::iterator itMask = masks.begin();
  std::vector<int>::iterator itTolerance = floodFillTolerances.begin();
  for(const auto &pt : ptsSeed) {

    CV_Assert(*itTolerance < 256);
    floodFillMask(imageSrc, pt, *itTolerance, *itMask);
    ++itTolerance;
    ++itMask;
  }

  outMask = cv::Mat::zeros(imageSrc.size(), CV_8UC1);
  for(itMask = masks.begin(); itMask != masks.end(); ++itMask) {

    CV_Assert((*itMask).size().width == outMask.size().width);
    CV_Assert((*itMask).size().height == outMask.size().height);

    for(int y = 0; y < itMask->rows; ++y){
      for(int x = 0; x < itMask->cols; ++x){
        
        if(itMask->at<uint8_t>(y, x) > 0) {
          outMask.at<uint8_t>(y, x) = itMask->at<uint8_t>(y, x);
        }
      }
    }
  }
}

double ImageUtils::resize(
	const cv::Mat &image,
  cv::Mat &matOut,
  double maxPixelsN)
{
  FUNCLOGTIMEL("ImageUtils::resize");

  if(image.cols * image.rows <= maxPixelsN)
  {
    image.copyTo(matOut);
    return 1.0;
  }

  double resizeFactor = std::sqrt(maxPixelsN / (image.cols * image.rows));
  cv::resize(
    image,
    matOut,
    cv::Size(0, 0),
    resizeFactor,
    resizeFactor,
    cv::INTER_AREA);

	return resizeFactor;
}

void ImageUtils::blendNone(
  const std::vector<TMat> &images,
  const std::vector<TMat> &masks,
  const std::vector<cv::Point> &tlCorners,
  TMat &outImage)
{
  FUNCLOGTIMEL("ImageUtils::blendNone");

  cv::detail::Blender blender;
  imgalign::blend(blender, images, masks, tlCorners, outImage);
}

void ImageUtils::featherBlend(
  const std::vector<TMat> &images,
  const std::vector<TMat> &masks,
  const std::vector<cv::Point> &tlCorners,
  double blendStrength,
  TMat &outImage)
{
  FUNCLOGTIMEL("ImageUtils::featherBlend");

  std::vector<cv::Size> sizes(images.size());
  for(size_t i = 0; i < images.size(); ++i) {
    sizes[i] = images[i].size();
  }
  auto blendWidth = getBlendWidth(blendStrength, tlCorners, sizes);
  cv::detail::FeatherBlender blender((float)(1.0 / blendWidth));
  imgalign::blend(blender, images, masks, tlCorners, outImage);
}

void ImageUtils::blendMultiBand(
  const std::vector<TMat> &images,
  const std::vector<TMat> &masks,
  const std::vector<cv::Point> &tlCorners,
  double blendStrength,
  TMat &outImage)
{
  FUNCLOGTIMEL("ImageUtils::blendMultiBand");

  std::vector<cv::Size> sizes(images.size());
  for(size_t i = 0; i < images.size(); ++i) {
    sizes[i] = images[i].size();
  }
  auto blendWidth = getBlendWidth(blendStrength, tlCorners, sizes);
  int numBands = ceil(log(blendWidth) / log(2.0)) - 1.0;

  cv::detail::MultiBandBlender blender(false, numBands);
  imgalign::blend(blender, images, masks, tlCorners, outImage);

  // auto box = bbox(images, tlCorners);
  // blender.prepare(cv::Rect(0, 0, box.width, box.height));

  // std::vector<TMat> images_16S3C(images.size());
  // for(size_t i = 0; i < images.size(); ++i) {
    
  //   cv::cvtColor(images[i], images_16S3C[i], CV_RGBA2RGB);
  //   images_16S3C[i].convertTo(images_16S3C[i], CV_16S);

  //   blender.feed(images_16S3C[i], masks[i], tlCorners[i]);
  // }

  // TMat dst, dstMask;
  // blender.blend(dst, dstMask);

  // dst.convertTo(dst, CV_8U);
  // TMat dst_rgba;
  // cv::cvtColor(dst, dst_rgba, CV_RGB2RGBA);

  // outImage = TMat::zeros(dst_rgba.size(), dst_rgba.type());
  // dst_rgba.copyTo(outImage, dstMask);

  // for(size_t i = 0; i < images.size(); ++i) {

  //   auto xAdd = tlCorners[i].x;
  //   auto yAdd = tlCorners[i].y;

  //   for(int y = 0; y < images[i].rows; ++y) {
  //     for(int x = 0; x < images[i].cols; ++x) {

  //       int y_ = y + yAdd;
  //       int x_ = x + xAdd;

  //       if(outImage.at<cv::Vec4b>(y_, x_)[3] != 0) {
  //         continue;
  //       }

  //       if(images[i].at<cv::Vec4b>(y, x)[3] < 255 && images[i].at<cv::Vec4b>(y, x)[3] > 0) {
  //         outImage.at<cv::Vec4b>(y_, x_)[0] = images[i].at<cv::Vec4b>(y, x)[0];
  //         outImage.at<cv::Vec4b>(y_, x_)[1] = images[i].at<cv::Vec4b>(y, x)[1];
  //         outImage.at<cv::Vec4b>(y_, x_)[2] = images[i].at<cv::Vec4b>(y, x)[2];
  //         outImage.at<cv::Vec4b>(y_, x_)[3] = images[i].at<cv::Vec4b>(y, x)[3];
  //       }
  //     }
  //   }
  // }
}

void
ImageUtils::compensateExposure(
  TConstPoints &corners,
  const std::vector<cv::UMat> &masks,
  std::vector<TMat> &images)
{
  FUNCLOGTIMEL("ImageUtils::compensateExposure");
  LogUtils::getLogUserInfo() << "Compensate exposure" << std::endl;

  size_t imagesN = images.size();

  std::vector<cv::UMat> imagesU(imagesN);
  for(size_t i = 0; i < imagesN; ++i) {

    TMat imageRgb;
    cv::cvtColor(images[i], imageRgb, CV_RGBA2RGB);
    imageRgb.copyTo(imagesU[i]);
  }

  cv::Ptr<cv::detail::ExposureCompensator> compensator =
    cv::detail::ExposureCompensator::createDefault(
      cv::detail::ExposureCompensator::CHANNELS_BLOCKS);

  compensator->feed(corners, imagesU, masks);
  
  for(size_t i = 0; i < imagesN; ++i) {

    TMat imageRgb;
    cv::cvtColor(images[i], imageRgb, CV_RGBA2RGB);
    
    compensator->apply(i, corners[i], imageRgb, masks[i]);

    auto itRgba = images[i].begin<cv::Vec4b>();
    for(auto itRgb = imageRgb.begin<cv::Vec3b>(); itRgb != imageRgb.end<cv::Vec3b>(); ++itRgb, ++itRgba) {

      (*itRgba)[0] = (*itRgb)[0];
      (*itRgba)[1] = (*itRgb)[1];
      (*itRgba)[2] = (*itRgb)[2];
    }
  }
}

void ImageUtils::stitch(
  TConstMat &src1,
  TConstMat &src2,
  bool exposureCompensate,
  BlendType blendType,
  double blendStrength,
  SeamFinderType seamFinderType,
  TMat &outDst)
{
  FUNCLOGTIMEL("ImageUtils::stitch");

  TMat mask1 = TMat::zeros(src1.size(), CV_8UC1);
  auto itMask1 = mask1.begin<uint8_t>();
  for(auto it = src1.begin<cv::Vec4b>(); it != src1.end<cv::Vec4b>(); ++it, ++itMask1) {
    if((*it)[3] == 255) {
      *itMask1 = (*it)[3];
    }
  }
  TMat mask2 = TMat::zeros(src2.size(), CV_8UC1);
  auto itMask2 = mask2.begin<uint8_t>();
  for(auto it = src2.begin<cv::Vec4b>(); it != src2.end<cv::Vec4b>(); ++it, ++itMask2) {
    if((*it)[3] == 255) {
      *itMask2 = (*it)[3];
    }
  }
  
  std::vector<TMat> images{src1, src2};
  std::vector<TMat> masks{ mask1, mask2};
  std::vector<cv::Point> tlCorners(2, cv::Point(0, 0));
  stitch(images, masks, tlCorners, exposureCompensate, blendType, blendStrength, seamFinderType, outDst);
}

void ImageUtils::stitch(
  std::vector<TMat> &images,
  std::vector<TMat> &masks,
  const std::vector<cv::Point> &tlCorners,
  bool exposureCompensate,
  BlendType blendType,
  double blendStrength,
  SeamFinderType seamFinderType,
  TMat &outDst)
{
  FUNCLOGTIMEL("ImageUtils::stitch");

  std::vector<cv::UMat> imagesU(images.size());
  std::vector<cv::UMat> masksU(images.size());

  cv::Ptr<cv::detail::SeamFinder> seamFinder;
  switch(seamFinderType) {
    case SeamFinderType::SFT_GRAPHCUT:
      seamFinder = cv::makePtr<cv::detail::GraphCutSeamFinder>(
        cv::detail::GraphCutSeamFinderBase::COST_COLOR);
      break;
    case SeamFinderType::SFT_VORNOI:
    default: {
      seamFinder = cv::makePtr<cv::detail::VoronoiSeamFinder>();
    }
  }

  for(size_t i = 0; i < images.size(); ++i) {
    if(seamFinderType != SeamFinderType::SFT_VORNOI) {
      cv::UMat t, t2;
      images[i].copyTo(t);
      cv::cvtColor(t, t2, CV_RGBA2RGB);
      t.release();
      t2.convertTo(imagesU[i], CV_32F);
    }
    else {
      images[i].copyTo(imagesU[i]);
    }
    masks[i].copyTo(masksU[i]);
  }

  LogUtils::getLogUserInfo() << "Finding seams" << std::endl;
  seamFinder->find(imagesU, tlCorners, masksU);

  if(exposureCompensate) {
    LogUtils::getLogUserInfo() << "Compensating exposure" << std::endl;
    compensateExposure(tlCorners, masksU, images);
  }

  for(size_t i = 0; i < images.size(); ++i) {
    masksU[i].copyTo(masks[i]);
  }

  LogUtils::getLogUserInfo() << "Blending" << std::endl;

  switch(blendType) {
    
    case BlendType::BT_FEATHER: {
      featherBlend(images, masks, tlCorners, blendStrength, outDst);
      break;
    }
    case BlendType::BT_NONE: {
      blendNone(images, masks, tlCorners, outDst);
      break;
    }
    case BlendType::BT_MULTIBAND:
    default: {
      blendMultiBand(images, masks, tlCorners, blendStrength, outDst);    
    }
  }
}

void ImageUtils::stitchFast(
  const std::vector<TMat> &images,
  const std::vector<TMat> &masks,
  const std::vector<cv::Point> &tlCorners,
  TMat &outDst)
{
  FUNCLOGTIMEL("ImageUtils::stitchFast");

  if(images.empty()) return;

  // for(auto &image : images) {
  //   LogUtils::getLog() << "stitchFast w/h " << image.size().width << "/" << image.size().height << std::endl;
  // }

  auto box = bbox(images, tlCorners);
  outDst = TMat::zeros(box, images[0].type());

  // LogUtils::getLog() << "stitchFast " << outDst.type() << " " <<
  //   outDst.size().width << "/" << outDst.size().height << std::endl;

  for(size_t i = 0; i < images.size(); ++i) {
    cv::Rect rRoi(tlCorners[i].x, tlCorners[i].y,
      images[i].size().width, images[i].size().height);

    TMat mRoi(outDst, rRoi);
    images[i].copyTo(mRoi, masks[i]);
  }

  // LogUtils::getLog() << "STITCHFAST END" << std::endl;
}

cv::Point ImageUtils::crop(
  TConstMat &src,
  TMat &outDst)
{
  FUNCLOGTIMEL("ImageUtils::crop");
  
  int l, t, r, b;
  l = t = std::numeric_limits<int>::max();
  r = b = 0;

  for(int y = 0; y < src.rows; ++y) {
    for(int x = 0; x < src.cols; ++x) {
      if(src.at<cv::Vec4b>(y, x)[3] > 0) {

        if(x < l) l = x;
        if(x > r) r = x;

        if(y < t) t = y;
        if(y > b) b = y;
      }
    }
  }

  if(r - l <= 0 || b - t <= 0) {
    outDst = TMat::zeros(0, 0, src.type());
  }
  else {
    outDst = src(cv::Rect(l, t, r - l, b - t));
  }

  return cv::Point(l, t);
}

class ColorTransfer {
  public:

    struct Stat {
      TMat mean, stdDev;
      void release() {
        mean.release();
        stdDev.release();
      }
    };
    struct Stats {
      Stat l, a, b;
      void release() {
        l.release();
        a.release();
        b.release();
      }
    };
    struct Channels {
      TMat l, a, b;
      void release() {
        l.release();
        a.release();
        b.release();
      }
    };

    static TMat convert(TConstMat &src) {

      TMat srcT, srcLab;
      cv::cvtColor(src, srcT, CV_RGBA2RGB);
      cv::cvtColor(srcT, srcLab, CV_RGB2Lab);
      srcLab.convertTo(srcT, CV_64F);
      return srcT;
    }

    static TMat convertInv(TConstMat &src) {
      TMat srcT1, srcT2, dst;
      src.convertTo(srcT1, CV_8U);
      cv::cvtColor(srcT1, srcT2, CV_Lab2RGB);
      cv::cvtColor(srcT2, dst, CV_RGB2RGBA);
      return dst;
    }

    static Channels split(TConstMat &src) {

      std::vector<TMat> channelsV;
      cv::split(src, channelsV);
      Channels channels;
      channels.l = channelsV[0];
      channels.a = channelsV[1];
      channels.b = channelsV[2];
      return channels;
    }

    static Stats stats(const Channels &ch) {

      Stats s;
      cv::meanStdDev(ch.l, s.l.mean, s.l.stdDev);
      cv::meanStdDev(ch.a, s.a.mean, s.a.stdDev);
      cv::meanStdDev(ch.b, s.b.mean, s.b.stdDev);
      return s;
    }

    static void clip(TMat &mat, double min, double max) {
      for(auto it = mat.begin<double>(); it != mat.end<double>(); ++it) {
        if(*it < 0) *it = min;
        else if(*it > 255) *it = max;
      }
    }

    static TMat calculate(TConstMat &src, TConstMat &dst) {
      
      TMat srcLab = convert(src);
      TMat dstLab = convert(dst);
      
      Channels srcC = split(srcLab);
      Channels dstC = split(dstLab);

      srcLab.release();
      dstLab.release();
      
      Stats srcS = stats(srcC);
      Stats dstS = stats(dstC);

      srcC.release();
      
      dstC.l -= dstS.l.mean;
      dstC.a -= dstS.a.mean;
      dstC.b -= dstS.b.mean;
      
      { TMat t;
        cv::divide(dstS.l.stdDev, srcS.l.stdDev, t);
        dstC.l = t.mul(dstC.l); }
      { TMat t;
        cv::divide(dstS.a.stdDev, srcS.a.stdDev, t);
        dstC.a = t.mul(dstC.a); }
      { TMat t;
        cv::divide(dstS.b.stdDev, srcS.b.stdDev, t);
        dstC.b = t.mul(dstC.b); }

      dstS.release();
      
      dstC.l += srcS.l.mean;
      dstC.a += srcS.a.mean;
      dstC.b += srcS.b.mean;

      srcS.release();
      
      // clip(dstC.l, 0.0f, 255.0f);
      // clip(dstC.a, 0.0f, 255.0f);
      // clip(dstC.b, 0.0f, 255.0f);

      std::vector<TMat> channelsV{dstC.l, dstC.a, dstC.b};
      TMat temp;
      cv::merge(channelsV, temp);
      dstC.release();
      return convertInv(temp);
    }
};


TMat ImageUtils::colorTransfer(
  TConstMat &src,
  TConstMat &dst)
{
  FUNCLOGTIMEL("ImageUtils::colorTransfer");

  return ColorTransfer::calculate(src, dst);
}

bool ImageUtils::estimateCorners(
  TConstMat &srcImage,
  cv::Point2f &tl, cv::Point2f &tr, cv::Point2f &br, cv::Point2f &bl)
{
  FUNCLOGTIMEL("ImageUtils::estimateCorners");

  double minDist = std::numeric_limits<double>::max();
  double d, cx, cy;;

  for(int y = 0; y < srcImage.rows / 2; ++y) {
    for(int x = 0; x < srcImage.cols / 2; ++x) {
      if(srcImage.at<cv::Vec4b>(y, x)[3] > 0) {
        
        d = x * x + y * y;  

        if(d < minDist) {
          tl.x = x;
          tl.y = y;
          minDist = d;
        }
      }
    }
  }
  if(minDist < 0) return false;

  minDist = std::numeric_limits<double>::max();
  for(int y = 0; y < srcImage.rows / 2; ++y) {
    for(int x = srcImage.cols / 2; x < srcImage.cols; ++x) {
      if(srcImage.at<cv::Vec4b>(y, x)[3] > 0) {
        
        cx = srcImage.cols - x;
        cy = y;

        d = cx * cx + cy * cy;

        if(d < minDist) {
          tr.x = x;
          tr.y = y;
          minDist = d;
        }
      }
    }
  }
  if(minDist < 0) return false;

  minDist = std::numeric_limits<double>::max();
  for(int y = srcImage.rows / 2; y < srcImage.rows; ++y) {
    for(int x = srcImage.cols / 2; x < srcImage.cols; ++x) {
      if(srcImage.at<cv::Vec4b>(y, x)[3] > 0) {
        
        cx = srcImage.cols - x;
        cy = srcImage.rows - y;

        d = cx * cx + cy * cy;

        if(d < minDist) {
          br.x = x;
          br.y = y;
          minDist = d;
        }
      }
    }
  }
  if(minDist < 0) return false;

  minDist = std::numeric_limits<double>::max();
  for(int y = srcImage.rows / 2; y < srcImage.rows; ++y) {
    for(int x = 0; x < srcImage.cols / 2; ++x) {
      if(srcImage.at<cv::Vec4b>(y, x)[3] > 0) {
        
        cx = x;
        cy = srcImage.rows - y;

        d = cx * cx + cy * cy;

        if(d < minDist) {
          bl.x = x;
          bl.y = y;
          minDist = d;
        }
      }
    }
  }
  if(minDist < 0) return false;

  return true;
}

bool ImageUtils::rectify(TConstMat &srcImage, TMat &dstImage)
{
  FUNCLOGTIMEL("ImageUtils::rectify");

  auto ptDist = [](const cv::Point2f &pt1, const cv::Point2f &pt2) {
    return std::sqrt(
      (pt1.x - pt2.x) * (pt1.x - pt2.x) + (pt1.y - pt2.y) * (pt1.y - pt2.y));
  };

  cv::Point2f tl, tr, br, bl;
  if(!ImageUtils::estimateCorners(srcImage, tl, tr, br, bl)) {
    LogUtils::getLog() << "Failed to estimate corners in image";
    return false;
  }

  auto topDimX = ptDist(tl, tr);
  auto bottomDimX = ptDist(bl, br);
  auto leftDimY = ptDist(tl, bl);
  auto rightDimY = ptDist(tr, br);
  
  double dimX = topDimX > bottomDimX ? topDimX : bottomDimX;
  double dimY = leftDimY > rightDimY ? leftDimY: rightDimY;
  cv::Size dstImageSize((int)dimX, int(dimY));

  cv::Point2f tlDst(0, 0);
  cv::Point2f trDst(dstImageSize.width, 0);
  cv::Point2f brDst(dstImageSize.width, dstImageSize.height);
  cv::Point2f blDst(0, dstImageSize.height);

  TPoints2f ptsSrc{tl, tr, br, bl};
  TPoints2f ptsDst{tlDst, trDst, brDst, blDst};

  auto matTransform = getPerspectiveTransform(ptsSrc, ptsDst);

  warpPerspective(srcImage, dstImage, matTransform, dstImageSize);

  return true;
}

} // imgalign