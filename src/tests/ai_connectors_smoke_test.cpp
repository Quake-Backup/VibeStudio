#include "core/ai_connectors.h"

#include <QCoreApplication>
#include <QSet>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QVector<slopstudio::AiCapabilityDescriptor> capabilities = slopstudio::aiCapabilityDescriptors();
	if (capabilities.size() < 10 || !slopstudio::aiCapabilityIds().contains(QStringLiteral("tool-calls"))) {
		return fail("Expected provider-neutral AI capabilities.");
	}
	if (!slopstudio::aiCapabilityForId(QStringLiteral("THREE_D"))) {
		return fail("Expected AI capability id normalization.");
	}

	const QVector<slopstudio::AiConnectorDescriptor> connectors = slopstudio::aiConnectorDescriptors();
	if (connectors.size() < 7) {
		return fail("Expected all planned AI connector stubs.");
	}

	QSet<QString> ids;
	for (const slopstudio::AiConnectorDescriptor& connector : connectors) {
		if (connector.id.isEmpty() || connector.displayName.isEmpty() || connector.providerFamily.isEmpty()) {
			return fail("Expected connector identity.");
		}
		if (ids.contains(connector.id)) {
			return fail("Expected connector ids to be unique.");
		}
		ids.insert(connector.id);
		if (connector.capabilities.isEmpty() || connector.authRequirement.isEmpty() || connector.privacyNote.isEmpty()) {
			return fail("Expected connector capability, auth, and privacy metadata.");
		}
		// The text connectors answer through core/ai_transport (HTTP, a harness,
		// or the GGUF runtime) and ElevenLabs makes sound effects; voice and 3D
		// generation remain design stubs.
		const bool textConnector = connector.id == QStringLiteral("openai") || connector.id == QStringLiteral("claude")
			|| connector.id == QStringLiteral("gemini") || connector.id == QStringLiteral("local-offline") || connector.id == QStringLiteral("custom-http")
			|| connector.id == QStringLiteral("claude-code") || connector.id == QStringLiteral("codex") || connector.id == QStringLiteral("custom-command")
			|| connector.id == QStringLiteral("local-gguf");
		if ((textConnector || connector.id == QStringLiteral("elevenlabs")) != connector.implemented) {
			return fail("Expected exactly the text connectors and ElevenLabs to be implemented.");
		}
	}

	if (!ids.contains(QStringLiteral("openai")) || !ids.contains(QStringLiteral("claude")) || !ids.contains(QStringLiteral("gemini")) || !ids.contains(QStringLiteral("elevenlabs")) || !ids.contains(QStringLiteral("meshy")) || !ids.contains(QStringLiteral("local-offline")) || !ids.contains(QStringLiteral("custom-http"))) {
		return fail("Expected named connector design stubs.");
	}
	if (!slopstudio::aiConnectorSupportsCapability(QStringLiteral("OpenAI"), QStringLiteral("tool_calls"))) {
		return fail("Expected connector capability normalization.");
	}
	if (slopstudio::aiConnectorSupportsCapability(QStringLiteral("elevenlabs"), QStringLiteral("three-d"))) {
		return fail("Expected unsupported connector capability to be rejected.");
	}
	if (slopstudio::aiModelDescriptors().size() < 7 || !slopstudio::aiModelForId(QStringLiteral("OPENAI_TEXT_DEFAULT"))) {
		return fail("Expected configurable AI model descriptors.");
	}
	if (!slopstudio::aiModelSupportsCapability(QStringLiteral("openai-text-default"), QStringLiteral("tool_calls"))) {
		return fail("Expected model capability normalization.");
	}
	if (slopstudio::aiCredentialStatusForConnector(QStringLiteral("local-offline")).configured != true) {
		return fail("Expected local/offline connector not to require cloud credentials.");
	}

	slopstudio::AiAutomationPreferences defaults = slopstudio::defaultAiAutomationPreferences();
	defaults.cloudConnectorsEnabled = true;
	defaults.agenticWorkflowsEnabled = true;
	defaults.preferredReasoningConnectorId = QStringLiteral("openai");
	const slopstudio::AiAutomationPreferences normalizedDefault = slopstudio::normalizedAiAutomationPreferences(defaults);
	if (!normalizedDefault.aiFreeMode || normalizedDefault.cloudConnectorsEnabled || normalizedDefault.agenticWorkflowsEnabled) {
		return fail("Expected AI-free mode to disable cloud and agentic settings.");
	}

	slopstudio::AiAutomationPreferences optIn;
	optIn.aiFreeMode = false;
	optIn.cloudConnectorsEnabled = true;
	optIn.agenticWorkflowsEnabled = true;
	optIn.preferredReasoningConnectorId = QStringLiteral("openai");
	optIn.preferredThreeDConnectorId = QStringLiteral("meshy");
	optIn.preferredVoiceConnectorId = QStringLiteral("meshy");
	optIn.preferredTextModelId = QStringLiteral("openai-text-default");
	optIn.preferredImageModelId = QStringLiteral("openai-text-default");
	const slopstudio::AiAutomationPreferences normalizedOptIn = slopstudio::normalizedAiAutomationPreferences(optIn);
	if (normalizedOptIn.aiFreeMode || !normalizedOptIn.cloudConnectorsEnabled || !normalizedOptIn.agenticWorkflowsEnabled) {
		return fail("Expected explicit AI opt-in preferences to persist.");
	}
	if (normalizedOptIn.preferredReasoningConnectorId != QStringLiteral("openai") || normalizedOptIn.preferredThreeDConnectorId != QStringLiteral("meshy") || !normalizedOptIn.preferredVoiceConnectorId.isEmpty()) {
		return fail("Expected preferred connector capability validation.");
	}
	if (normalizedOptIn.preferredTextModelId != QStringLiteral("openai-text-default") || !normalizedOptIn.preferredImageModelId.isEmpty()) {
		return fail("Expected preferred model capability validation.");
	}
	if (!slopstudio::aiAutomationPreferencesText(normalizedOptIn).contains(QStringLiteral("OpenAI"))) {
		return fail("Expected AI preferences summary text.");
	}

	return EXIT_SUCCESS;
}
