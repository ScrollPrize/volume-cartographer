1. Build the ubuntu-24.04-noble-s3.Dockerfile

2. Login into aws if needed, e.g. you have an sso configured.

3. Run the docker with the following command, putting the right <BUCKET-NAME> and <YOUR-PROFILE>:


xhost +local:docker
sudo docker run -it --rm \
  --cap-add SYS_ADMIN \
  --device /dev/fuse \
  --security-opt apparmor:unconfined \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v ~/.aws:/root/.aws \
  -e DISPLAY=$DISPLAY \
  -e BUCKET=<BUCKET-NAME> \
  -e PROFILE=<YOUR-PROFILE> \
  -e QT_QPA_PLATFORM=xcb \
  -e QT_X11_NO_MITSHM=1 \
 vc3d