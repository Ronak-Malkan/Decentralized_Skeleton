python3 -m grpc_tools.protoc \
  -I proto \
  --python_out=client \
  --grpc_python_out=client \
  proto/mini3.proto

mkdir -p generated

protoc -I=proto \
  --cpp_out=generated \
  --grpc_out=generated \
  --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
  proto/mini3.proto