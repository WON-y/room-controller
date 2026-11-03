Edge Impulse 에서 Deployment → Arduino library 로 받은 zip 을 이 폴더에 풀어 넣으세요.
(예: lib/room_inferencing/ → 헤더 room_inferencing.h)
Edge Impulse 프로젝트 이름을 "room" 으로 만들면 헤더 이름이 room_inferencing.h 가 됩니다.
다른 이름이면 src/infer/main.cpp 의 #include 두 군데를 바꾸세요.
