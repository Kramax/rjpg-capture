for x in 0 1 2 3 4 5 6 7 8 9; do
  convert test-image.jpg -virtual-pixel black -distort SRT $(( x * 36 )) test-image-$x.jpg
  #convert test-image.jpg -rotate $(( x * 36 )) test-image-$x.jpg
done
