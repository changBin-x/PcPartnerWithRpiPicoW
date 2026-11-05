#include "weather_service.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_body(char* response, const char* expected) {
  size_t length = strlen(response);
  size_t body_length = 0;
  const char* body = weather_service_http_body(response, length, &body_length);
  assert(body != NULL);
  assert(body_length == strlen(expected));
  assert(memcmp(body, expected, body_length) == 0);
  printf("  HTTP 响应体解析：%s\n", expected);
}

static void expect_json(const char* json, const char* city, const char* weather,
                        int temperature) {
  WeatherData data;
  memset(&data, 0, sizeof(data));
  assert(weather_service_parse_json(json, &data));
  assert(strcmp(data.city, city) == 0);
  assert(strcmp(data.weather, weather) == 0);
  assert(data.temperature == temperature);
  assert(data.valid);
  assert(!data.is_stale);
  printf("  JSON -> %s %s %d℃\n", data.city, data.weather, data.temperature);
}

int main(void) {
  printf("== test_weather_service ==\n");

  char content_length[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
  expect_body(content_length, "hello");

  char chunked[] =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
      "5\r\nhello\r\n0\r\n\r\n";
  expect_body(chunked, "hello");

  char not_found[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
  size_t body_length = 0;
  assert(weather_service_http_body(not_found, strlen(not_found),
                                   &body_length) == NULL);

  char truncated[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhi";
  assert(weather_service_http_body(truncated, strlen(truncated),
                                   &body_length) == NULL);

  char incomplete[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n";
  assert(weather_service_http_body(incomplete, strlen(incomplete),
                                   &body_length) == NULL);

  expect_json(
      "{\"status\":\"1\",\"lives\":[{\"province\":\"浙江\",\"city\":\"宁波\","
      "\"weather\":\"雾\",\"temperature\":\"16\"}]}",
      "宁波", "雾", 16);

  expect_json(
      "{\"status\":\"1\",\"lives\":[{\"province\":\"浙江\",\"city\":\"[]\","
      "\"weather\":\"晴\",\"temperature\":25.6}]}",
      "浙江", "晴", 25);

  WeatherData data;
  memset(&data, 0, sizeof(data));
  assert(!weather_service_parse_json("{\"status\":\"0\",\"lives\":[]}", &data));
  assert(!weather_service_parse_json("{\"status\":\"1\",\"lives\":[]} trailing",
                                     &data));
  assert(!weather_service_parse_json(
      "{\"status\":\"1\",\"lives\":[{\"city\":\"宁波\",\"weather\":\"\","
      "\"temperature\":\"16\"}]}",
      &data));
  assert(!weather_service_parse_json(
      "{\"status\":\"1\",\"lives\":[{\"city\":\"宁波\",\"weather\":\"雾\","
      "\"temperature\":\"100\"}]}",
      &data));
  assert(!weather_service_parse_json("not json", &data));
  assert(!weather_service_parse_json(NULL, &data));
  assert(!weather_service_parse_json("{}", NULL));

  /* 解析失败不得改写输出。 */
  memset(&data, 0, sizeof(data));
  data.temperature = 7;
  assert(!weather_service_parse_json("not json", &data));
  assert(data.temperature == 7);

  /* 默认城市 adcode 必须与宁波一致。 */
  assert(strcmp(kWeatherDefaultAdcode, "330200") == 0);

  printf("test_weather_service PASS\n");
  return 0;
}
