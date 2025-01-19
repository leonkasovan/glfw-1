#include <GLFW/glfw3.h>
#include <stdio.h>

void printGamepadState(int jid) {
	if (glfwJoystickIsGamepad(jid)) {
		const char* gamepadName = glfwGetGamepadName(jid);
		printf("Gamepad Name: %s\n", gamepadName);

		GLFWgamepadstate state;
		if (glfwGetGamepadState(jid, &state)) {
			// Print button states
			printf("Buttons: ");
			for (int i = 0; i <= GLFW_GAMEPAD_BUTTON_LAST; ++i) {
				printf("%d ", state.buttons[i]);
			}
			printf("\n");

			// Print axis states
			printf("Axes: ");
			for (int i = 0; i <= GLFW_GAMEPAD_AXIS_LAST; ++i) {
				printf("%f ", state.axes[i]);
			}
			printf("\n");
		}
	}
	else {
		printf("No gamepad connected at %d\n", jid);
	}
}

int main() {
	if (!glfwInit()) {
		fprintf(stderr, "Failed to initialize GLFW\n");
		return -1;
	}

	// Create a windowed mode window and its OpenGL context
	GLFWwindow* window = glfwCreateWindow(640, 480, "Gamepad Example", NULL, NULL);
	if (!window) {
		glfwTerminate();
		return -1;
	}

	// Make the window's context current
	glfwMakeContextCurrent(window);

	printf("Press ESC to exit\n");

	// Main loop
	while (!glfwWindowShouldClose(window)) {
		// Check for the first connected gamepad (e.g., GLFW_JOYSTICK_1)
		printGamepadState(GLFW_JOYSTICK_1);

		// Poll for and process events
		glfwPollEvents();

		// Exit if ESC is pressed
		if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
			glfwSetWindowShouldClose(window, GLFW_TRUE);
		}
	}

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}